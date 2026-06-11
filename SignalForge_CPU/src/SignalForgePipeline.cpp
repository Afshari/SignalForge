#include "SignalForgePipeline.h"
#include "WavReader.h"
#include "RedisClient.h"
#include "cpu/SignalForge.h"
#include "Utils.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <xxhash.h>

namespace SignalForge {

	// --------------------------------------------------------------------------------
	SignalForgePipeline::SignalForgePipeline(
		const std::vector<std::string>& filepaths,
		Config config, bool sha256_mode)
		: m_filepaths(filepaths)
		, m_config(std::move(config))
		, m_sha256_mode(sha256_mode)
	{
		m_reader_batch_size = m_sha256_mode
			? m_config.sha256.batch_size
			: m_config.fft.batch_size;
	}

	// --------------------------------------------------------------------------------
	void SignalForgePipeline::Run()
	{
		m_grpc_receiver = std::make_unique<GrpcReceiverThread>(
			m_path_queue,
			m_config.input_dir.string(),
			"0.0.0.0:50051");
		m_grpc_receiver->Start();


		// --- Thread 1: Scanner ---
		// Pushes each filepath into m_path_queue then closes it.
		// gRPC will push into m_path_queue here later as a second producer.
		std::jthread t_scanner([this]() { RunScannerThread(); });

		// --- Thread 2: Reader ---
		// Pops file paths, reads PCM data, accumulates up to m_reader_batch_size,
		// then pushes a WavBatch into m_wav_queue.
		std::vector<std::jthread> t_readers;
		for (uint32_t r = 0; r < m_config.reader_threads; r++)
			t_readers.emplace_back([this]() { RunReaderThread(); });

		// --- Thread 3: GPU worker ---
		// Pops WavBatch, runs SHA-256, filters duplicates via Redis,
		// accumulates survivors up to fft batch size, runs FFT, pushes FFTResult.
		std::jthread t_gpu([this]() {
			if (m_sha256_mode) RunGpuThreadSha256();
			else               RunGpuThread();
			});

		// --- Thread 4: Redis writer ---
		// Pops FFTResult batches and stores magnitudes in Redis.
		std::jthread t_writer([this]() {
			if (m_sha256_mode) RunWriterThreadSha256();
			else               RunWriterThread();
			});

		if (!m_filepaths.empty())
			t_scanner.join();
		else
			m_grpc_receiver->Wait(); // blocks until Ctrl+C calls Stop()

		m_grpc_receiver->Stop();
		// All four jthreads go out of scope here and join automatically.
		// Run() blocks until all threads have finished.
	}

	void SignalForgePipeline::RunScannerThread() {
		for (const auto& path : m_filepaths)
			m_path_queue.push(path);

		std::cout << "[Scanner] Done. Pushed " << m_filepaths.size()
			<< " paths." << std::endl;
	}

	void SignalForgePipeline::RunReaderThread() {
		WavBatch batch;
		size_t total = 0;

		while (auto path = m_path_queue.pop())
		{
			try
			{
				WavReader reader(*path);
				batch.pcm_data.push_back(reader.ReadPCM());
				total++;

				if (batch.pcm_data.size() >= m_reader_batch_size)
				{
					m_wav_queue.push(std::move(batch));
					batch = WavBatch{};
				}
			}
			catch (const std::exception& e)
			{
				std::cerr << "[Reader] Skipping " << *path
					<< " - " << e.what() << std::endl;
			}
		}

		// push remaining partial batch
		if (!batch.pcm_data.empty())
			m_wav_queue.push(std::move(batch));

		// last reader closes the wav queue
		if (++m_readers_done == static_cast<int>(m_config.reader_threads))
		{
			m_wav_queue.close();
			std::cout << "[Reader] Done. All readers finished." << std::endl;
		}
	}

	void SignalForgePipeline::RunGpuThreadSha256() {

		RedisClient redis(m_config.redis.host, m_config.redis.port, m_config.redis.db);
		bool redisAvailable = redis.Connect();
		if (!redisAvailable)
			std::cerr << "[GPU] Redis not available - duplicate skipping disabled." << std::endl;

		auto t_run_start = std::chrono::high_resolution_clock::now();

		// timing accumulators
		double t_sha_ms = 0.0;
		double t_redis_ms = 0.0;
		double t_fft_ms = 0.0;
		double t_wait_ms = 0.0;

		// Accumulator - survivors waiting for FFT
		std::vector<std::vector<uint8_t>> accum_pcm;
		size_t skipped = 0;
		size_t total_processed = 0;
		std::vector<std::string>          accum_hashes;

		uint32_t half = m_config.fft_size / 2 + 1;

		auto flushFFT = [&]()
			{
				if (accum_pcm.empty()) return;

				uint32_t count = static_cast<uint32_t>(accum_pcm.size());
				std::vector<float> magnitudes((uint64_t)count * half, 0.0f);

				auto t_fft0 = std::chrono::high_resolution_clock::now();
				FFTBatchWrapper_CPU(accum_pcm, magnitudes.data(), count,
					m_config.fft_size, m_config.fft.threads_per_block);
				t_fft_ms += std::chrono::duration<double, std::milli>(
					std::chrono::high_resolution_clock::now() - t_fft0).count();

				FFTResult result;
				result.hashes = std::move(accum_hashes);
				result.magnitudes = std::move(magnitudes);
				result.count = count;
				result.half = half;

				m_result_queue.push(std::move(result));

				accum_pcm.clear();
				accum_hashes.clear();
			};

		while (true)
		{
			auto t_wait0 = std::chrono::high_resolution_clock::now();
			auto batch = m_wav_queue.pop();
			t_wait_ms += std::chrono::duration<double, std::milli>(
				std::chrono::high_resolution_clock::now() - t_wait0).count();
			if (!batch) break;

			uint32_t count = static_cast<uint32_t>(batch->pcm_data.size());
			std::vector<uint64_t> batchHashes(count * 4, 0);

			// Run SHA-256 on entire batch
			auto t_sha0 = std::chrono::high_resolution_clock::now();
			SHA256BatchWrapper_CPU(batch->pcm_data, batchHashes.data(), count,
				m_config.sha256.threads_per_block);
			t_sha_ms += std::chrono::duration<double, std::milli>(
				std::chrono::high_resolution_clock::now() - t_sha0).count();

			// Filter duplicates - add survivors to accumulator
			for (uint32_t i = 0; i < count; i++)
			{
				std::string hex = Utils::HashToHex(batchHashes.data() + i * 4);
				total_processed++;

				auto t_redis0 = std::chrono::high_resolution_clock::now();
				if (redisAvailable && redis.HashExists(hex)) {
					t_redis_ms += std::chrono::duration<double, std::milli>(
						std::chrono::high_resolution_clock::now() - t_redis0).count();
					skipped++;
					continue;
				}
				if (redisAvailable)
					redis.SetHash(hex, Utils::NowISO8601());
				t_redis_ms += std::chrono::duration<double, std::milli>(
					std::chrono::high_resolution_clock::now() - t_redis0).count();

				accum_pcm.push_back(std::move(batch->pcm_data[i]));
				accum_hashes.push_back(std::move(hex));

				// Flush accumulator when FFT batch size is reached
				if (accum_pcm.size() >= m_config.fft.batch_size)
					flushFFT();
			}
		}

		// Flush any remaining survivors
		flushFFT();

		m_result_queue.close();
		std::cout << "[GPU] Done. Skipped " << skipped
			<< " duplicates, sent " << (total_processed - skipped)
			<< " to FFT." << std::endl;

		if (m_config.verbose)
		{
			auto t_run_end = std::chrono::high_resolution_clock::now();
			double total_ms = std::chrono::duration<double, std::milli>(
				t_run_end - t_run_start).count();
			std::cout << std::fixed << std::setprecision(3)
				<< "[GPU] Total:   " << total_ms / 1000.0 << "s\n"
				<< "[GPU] SHA-256: " << t_sha_ms / 1000.0 << "s\n"
				<< "[GPU] FFT:     " << t_fft_ms / 1000.0 << "s\n"
				<< "[GPU] Redis:   " << t_redis_ms / 1000.0 << "s\n"
				<< "[GPU] Waiting: " << t_wait_ms / 1000.0 << "s\n"
				<< std::endl;
		}
	}

	void SignalForgePipeline::RunGpuThread() {

		auto t_run_start = std::chrono::high_resolution_clock::now();

		double t_fft_ms = 0.0;
		double t_wait_ms = 0.0;

		std::vector<std::vector<uint8_t>> accum_pcm;
		uint32_t half = m_config.fft_size / 2 + 1;

		auto flushFFT = [&]()
			{
				if (accum_pcm.empty()) return;

				uint32_t count = static_cast<uint32_t>(accum_pcm.size());
				std::vector<float> magnitudes((uint64_t)count * half, 0.0f);

				auto t_fft0 = std::chrono::high_resolution_clock::now();
				FFTBatchWrapper_CPU(accum_pcm, magnitudes.data(), count,
					m_config.fft_size, m_config.fft.threads_per_block);
				t_fft_ms += std::chrono::duration<double, std::milli>(
					std::chrono::high_resolution_clock::now() - t_fft0).count();

				FFTResult result;
				result.magnitudes = std::move(magnitudes);
				result.count = count;
				result.half = half;

				m_result_queue.push(std::move(result));
				accum_pcm.clear();
			};

		while (true)
		{
			auto t_wait0 = std::chrono::high_resolution_clock::now();
			auto batch = m_wav_queue.pop();
			t_wait_ms += std::chrono::duration<double, std::milli>(
				std::chrono::high_resolution_clock::now() - t_wait0).count();
			if (!batch) break;

			for (auto& pcm : batch->pcm_data)
			{
				accum_pcm.push_back(std::move(pcm));
				if (accum_pcm.size() >= m_config.fft.batch_size)
					flushFFT();
			}
		}

		flushFFT();
		m_result_queue.close();

		std::cout << "[GPU] Done." << std::endl;

		if (m_config.verbose)
		{
			auto t_run_end = std::chrono::high_resolution_clock::now();
			double total_ms = std::chrono::duration<double, std::milli>(
				t_run_end - t_run_start).count();
			std::cout << std::fixed << std::setprecision(3)
				<< "[GPU] Total:   " << total_ms / 1000.0 << "s\n"
				<< "[GPU] FFT:     " << t_fft_ms / 1000.0 << "s\n"
				<< "[GPU] Waiting: " << t_wait_ms / 1000.0 << "s\n"
				<< std::endl;
		}
	}

	void SignalForgePipeline::RunWriterThreadSha256() {
		RedisClient redis(m_config.redis.host, m_config.redis.port, m_config.redis.db);
		bool redisAvailable = redis.Connect();
		if (!redisAvailable)
			std::cerr << "[Writer] Redis not available - results won't be stored." << std::endl;

		size_t total = 0;
		size_t skipped = 0;
		double t_xxhash_ms = 0.0;
		double t_redis_ms = 0.0;

		while (auto result = m_result_queue.pop())
		{
			if (!redisAvailable) continue;

			for (uint32_t i = 0; i < result->count; i++)
			{
				const float* mag_ptr = result->magnitudes.data() + (uint64_t)i * result->half;
				size_t mag_bytes = (size_t)result->half * sizeof(float);

				auto t_xx0 = std::chrono::high_resolution_clock::now();
				uint64_t hash64 = XXH64(mag_ptr, mag_bytes, 0);
				t_xxhash_ms += std::chrono::duration<double, std::milli>(
					std::chrono::high_resolution_clock::now() - t_xx0).count();

				std::ostringstream oss;
				oss << std::hex << std::setw(16) << std::setfill('0') << hash64;
				std::string xxhash_hex = oss.str();

				auto t_redis0 = std::chrono::high_resolution_clock::now();
				if (redis.FftMagSha256Exists(xxhash_hex))
				{
					t_redis_ms += std::chrono::duration<double, std::milli>(
						std::chrono::high_resolution_clock::now() - t_redis0).count();
					skipped++;
					continue;
				}

				redis.SetFftMagSha256(xxhash_hex, mag_ptr, result->half);
				t_redis_ms += std::chrono::duration<double, std::milli>(
					std::chrono::high_resolution_clock::now() - t_redis0).count();
				total++;
			}
		}

		std::cout << "[Writer] Done. Stored " << total
			<< " files, skipped " << skipped << " FFT duplicates." << std::endl;

		if (m_config.verbose)
		{
			std::cout << std::fixed << std::setprecision(3)
				<< "[Writer] xxHash: " << t_xxhash_ms / 1000.0 << "s\n"
				<< "[Writer] Redis:  " << t_redis_ms / 1000.0 << "s\n"
				<< std::endl;
		}
	}


	void SignalForgePipeline::RunWriterThread() {
		RedisClient redis(m_config.redis.host, m_config.redis.port, m_config.redis.db);
		bool redisAvailable = redis.Connect();
		if (!redisAvailable)
			std::cerr << "[Writer] Redis not available - results won't be stored." << std::endl;

		size_t total = 0;
		size_t skipped = 0;
		double t_xxhash_ms = 0.0;
		double t_redis_ms = 0.0;

		while (auto result = m_result_queue.pop())
		{
			if (!redisAvailable) continue;

			for (uint32_t i = 0; i < result->count; i++)
			{
				const float* mag_ptr = result->magnitudes.data() + (uint64_t)i * result->half;
				size_t mag_bytes = (size_t)result->half * sizeof(float);

				// compute xxHash64 of magnitude array
				auto t_xx0 = std::chrono::high_resolution_clock::now();
				uint64_t hash64 = XXH64(mag_ptr, mag_bytes, 0);
				t_xxhash_ms += std::chrono::duration<double, std::milli>(
					std::chrono::high_resolution_clock::now() - t_xx0).count();

				// format as 16-char hex string
				std::ostringstream oss;
				oss << std::hex << std::setw(16) << std::setfill('0') << hash64;
				std::string xxhash_hex = oss.str();

				// check for duplicate FFT result
				auto t_redis0 = std::chrono::high_resolution_clock::now();
				if (redis.FftMagExists(xxhash_hex))
				{
					t_redis_ms += std::chrono::duration<double, std::milli>(
						std::chrono::high_resolution_clock::now() - t_redis0).count();
					skipped++;
					continue;
				}

				redis.SetFftMag(xxhash_hex, mag_ptr, result->half);
				t_redis_ms += std::chrono::duration<double, std::milli>(
					std::chrono::high_resolution_clock::now() - t_redis0).count();
				total++;
			}
		}

		std::cout << "[Writer] Done. Stored " << total
			<< " files, skipped " << skipped << " FFT duplicates." << std::endl;

		if (m_config.verbose)
		{
			std::cout << std::fixed << std::setprecision(3)
				<< "[Writer] xxHash: " << t_xxhash_ms / 1000.0 << "s\n"
				<< "[Writer] Redis:  " << t_redis_ms / 1000.0 << "s\n"
				<< std::endl;
		}
	}

	void SignalForgePipeline::Stop()
	{
		if (m_grpc_receiver)
			m_grpc_receiver->Stop();
	}

} // namespace SignalForge