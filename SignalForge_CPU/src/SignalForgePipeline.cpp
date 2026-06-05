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

namespace SignalForge {

	// --------------------------------------------------------------------------------
	SignalForgePipeline::SignalForgePipeline(
		const std::vector<std::string>& filepaths,
		Config config)
		: m_filepaths(filepaths)
		, m_config(std::move(config))
	{
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
		std::jthread t_scanner([this]()
			{
				for (const auto& path : m_filepaths)
					m_path_queue.push(path);

				std::cout << "[Scanner] Done. Pushed " << m_filepaths.size()
					<< " paths." << std::endl;
			});

		// --- Thread 2: Reader ---
		// Pops file paths, reads PCM data, accumulates up to sha256 batch size,
		// then pushes a WavBatch into m_wav_queue.
		std::vector<std::jthread> t_readers;
		for (uint32_t r = 0; r < m_config.reader_threads; r++)
		{
			t_readers.emplace_back([this]()
				{
					WavBatch batch;
					size_t total = 0;

					while (auto path = m_path_queue.pop())
					{
						try
						{
							WavReader reader(*path);
							batch.pcm_data.push_back(reader.ReadPCM());
							total++;

							if (batch.pcm_data.size() >= m_config.sha256.batch_size)
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
				});
		}

		// --- Thread 3: GPU worker ---
		// Pops WavBatch, runs SHA-256, filters duplicates via Redis,
		// accumulates survivors up to fft batch size, runs FFT, pushes FFTResult.
		std::jthread t_gpu([this]()
			{
				RedisClient redis;
				bool redisAvailable = redis.Connect();
				if (!redisAvailable)
					std::cerr << "[GPU] Redis not available - duplicate skipping disabled." << std::endl;

				auto t_run_start = std::chrono::high_resolution_clock::now();

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

						FFTBatchWrapper_CPU(accum_pcm, magnitudes.data(), count,
							m_config.fft_size, m_config.fft.threads_per_block);

						FFTResult result;
						result.hashes = std::move(accum_hashes);
						result.magnitudes = std::move(magnitudes);
						result.count = count;
						result.half = half;

						m_result_queue.push(std::move(result));

						accum_pcm.clear();
						accum_hashes.clear();
					};

				while (auto batch = m_wav_queue.pop())
				{
					uint32_t count = static_cast<uint32_t>(batch->pcm_data.size());
					std::vector<uint64_t> batchHashes(count * 4, 0);

					// Run SHA-256 on entire batch
					SHA256BatchWrapper_CPU(batch->pcm_data, batchHashes.data(), count,
						m_config.sha256.threads_per_block);

					// Filter duplicates - add survivors to accumulator
					for (uint32_t i = 0; i < count; i++)
					{
						std::string hex = Utils::HashToHex(batchHashes.data() + i * 4);

						total_processed++;
						if (redisAvailable && redis.HashExists(hex)) {
							skipped++;
							continue;
						}

						// Store hash in Redis with timestamp
						if (redisAvailable)
							redis.SetHash(hex, Utils::NowISO8601());

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
					double ms = std::chrono::duration<double, std::milli>(t_run_end - t_run_start).count();
					std::cout << "[Pipeline] GPU time: " << std::fixed << std::setprecision(3)
						<< ms / 1000.0 << "s" << std::endl;
				}
			});

		// --- Thread 4: Redis writer ---
		// Pops FFTResult batches and stores magnitudes in Redis.
		std::jthread t_writer([this]()
			{
				RedisClient redis;
				bool redisAvailable = redis.Connect();
				if (!redisAvailable)
					std::cerr << "[Writer] Redis not available - results won't be stored." << std::endl;

				size_t total = 0;

				while (auto result = m_result_queue.pop())
				{
					if (!redisAvailable) continue;

					for (uint32_t i = 0; i < result->count; i++)
					{
						redis.SetMagnitudes(
							result->hashes[i],
							result->magnitudes.data() + (uint64_t)i * result->half,
							result->half);
						total++;
					}
				}

				std::cout << "[Writer] Done. Stored magnitudes for "
					<< total << " files." << std::endl;
			});


		if (!m_filepaths.empty())
			t_scanner.join();
		else
			m_grpc_receiver->Wait(); // blocks until Ctrl+C calls Stop()

		m_grpc_receiver->Stop();
		// All four jthreads go out of scope here and join automatically.
		// Run() blocks until all threads have finished.
	}

	void SignalForgePipeline::Stop()
	{
		if (m_grpc_receiver)
			m_grpc_receiver->Stop();
	}

} // namespace SignalForge