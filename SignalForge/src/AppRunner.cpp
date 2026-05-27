#include "AppRunner.h"
#include "WavReader.h"
#include "RedisClient.h"
#include "cpu/SignalForge.h"
#include "Utils.h"
#include <boost/filesystem.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace SignalForge {

    // --------------------------------------------------------------------------------
    static std::string HashToHex(const uint64_t* h_hash)
    {
        std::ostringstream oss;
        for (int i = 0; i < 4; i++)
            oss << std::hex << std::setw(16) << std::setfill('0') << h_hash[i];
        return oss.str();
    }

    // --------------------------------------------------------------------------------
    static void WriteResultsCsv(
        const std::filesystem::path& outputDir,
        const std::vector<std::string>& hashes,
        const std::vector<double>& durationsMs)
    {
        std::filesystem::create_directories(outputDir);
        auto csvPath = outputDir / "hash_results.csv";

        std::ofstream csv(csvPath);
        csv << "sha256,duration_ms\n";

        for (size_t i = 0; i < hashes.size(); i++)
            csv << hashes[i] << ","
            << std::fixed << std::setprecision(3) << durationsMs[i] << "\n";

        std::cout << "[INFO] Results written to: " << csvPath << std::endl;
    }

    // --------------------------------------------------------------------------------
    static void WriteProfileCsv(
        const std::filesystem::path& outputDir,
        uint32_t                     threadsPerBlock,
        uint32_t                     batchSize,
        const std::vector<double>& batchDurationsMs,
        size_t                       totalFiles,
        double                       totalDurationMs)
    {
        std::filesystem::create_directories(outputDir);
        auto csvPath = outputDir / "profile_results.csv";

        bool writeHeader = !std::filesystem::exists(csvPath);
        std::ofstream csv(csvPath, std::ios::app);

        if (writeHeader)
            csv << "threads_per_block,batch_size,total_files,"
            << "total_duration_ms,avg_batch_duration_ms,"
            << "throughput_files_per_sec\n";

        double avgBatch = totalDurationMs / batchDurationsMs.size();
        double throughput = totalFiles / (totalDurationMs / 1000.0);

        csv << threadsPerBlock << ","
            << batchSize << ","
            << totalFiles << ","
            << std::fixed << std::setprecision(3) << totalDurationMs << ","
            << avgBatch << ","
            << std::setprecision(1) << throughput << "\n";

        std::cout << "[INFO] Profile results appended to: " << csvPath << std::endl;
    }

    // --------------------------------------------------------------------------------
    static void WriteFFTCsv(
        const std::filesystem::path& outputDir,
        const std::vector<double>& durationsMs)
    {
        std::filesystem::create_directories(outputDir);
        auto csvPath = outputDir / "fft_results.csv";

        std::ofstream csv(csvPath);
        csv << "duration_ms,throughput_files_per_sec\n";

        for (size_t i = 0; i < durationsMs.size(); i++)
            csv << std::fixed << std::setprecision(3) << durationsMs[i] << ","
            << std::setprecision(1) << (1000.0 / durationsMs[i]) << "\n";

        std::cout << "[INFO] FFT results written to: " << csvPath << std::endl;
    }

    // --------------------------------------------------------------------------------
    int AppRunner::RunHash(const Config& config)
    {
        if (!std::filesystem::exists(config.input_dir))
        {
            std::cerr << "[ERROR] Input directory not found: " << config.input_dir << std::endl;
            return 1;
        }

        auto files = Utils::ScanWavFiles(config.input_dir);
        if (files.empty())
        {
            std::cout << "[INFO] No .wav files found in: " << config.input_dir << std::endl;
            return 0;
        }

        std::cout << "[INFO] Found " << files.size() << " .wav files." << std::endl;

        // --- Connect to Redis ---
        RedisClient redis;
        bool redisAvailable = redis.Connect();
        if (redisAvailable)
            std::cout << "[INFO] Redis connected - duplicate skipping enabled." << std::endl;
        else
            std::cout << "[WARN] Redis not available - processing all files." << std::endl;

        std::vector<std::string> hashes;
        std::vector<double>      durations;
        size_t                   skipped = 0;

        size_t batchSize = config.sha256.batch_size;
        for (size_t start = 0; start < files.size(); start += batchSize)
        {
            size_t end = std::min(start + batchSize, files.size());
            std::vector<std::vector<uint8_t>> inputs;

            for (size_t i = start; i < end; i++)
            {
                WavReader reader(files[i]);
                inputs.push_back(reader.ReadPCM());
            }

            if (inputs.empty()) continue;

            uint32_t count = static_cast<uint32_t>(inputs.size());
            std::vector<uint64_t> batchHashes(count * 4, 0);

            auto t0 = std::chrono::high_resolution_clock::now();
            SHA256BatchWrapper_CPU(inputs, batchHashes.data(), count,
                config.sha256.threads_per_block);
            auto t1 = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            for (uint32_t i = 0; i < count; i++)
            {
                std::string hex = Utils::HashToHex(batchHashes.data() + i * 4);

                if (redisAvailable && redis.HashExists(hex))
                {
                    skipped++;
                    continue;
                }

                hashes.push_back(hex);
                durations.push_back(ms / count);

                if (redisAvailable)
                    redis.SetHash(hex, Utils::NowISO8601());
            }
        }

        if (skipped > 0)
            std::cout << "[INFO] Skipped " << skipped << " already-hashed files." << std::endl;

        WriteResultsCsv(config.output_dir, hashes, durations);
        std::cout << "[INFO] Done. Processed " << files.size() << " files." << std::endl;
        return 0;
    }

    // --------------------------------------------------------------------------------
    int AppRunner::RunProfile(const Config& config)
    {
        if (!std::filesystem::exists(config.input_dir))
        {
            std::cerr << "[ERROR] Input directory not found: " << config.input_dir << std::endl;
            return 1;
        }

        auto files = Utils::ScanWavFiles(config.input_dir);
        if (files.empty())
        {
            std::cout << "[INFO] No .wav files found in: " << config.input_dir << std::endl;
            return 0;
        }

        std::cout << "[INFO] Profile mode" << std::endl;
        std::cout << "[INFO] Found " << files.size() << " .wav files." << std::endl;
        std::cout << "[INFO] Batch size: " << config.sha256.batch_size << std::endl;
        std::cout << "[INFO] Threads/block: " << config.sha256.threads_per_block << std::endl;

        std::vector<double> batchDurations;
        double              totalDurationMs = 0.0;
        size_t              batchSize = config.sha256.batch_size;

        for (size_t start = 0; start < files.size(); start += batchSize)
        {
            size_t end = std::min(start + batchSize, files.size());
            std::vector<std::vector<uint8_t>> inputs;

            for (size_t i = start; i < end; i++)
            {
                WavReader reader(files[i]);
                inputs.push_back(reader.ReadPCM());
            }

            uint32_t count = static_cast<uint32_t>(inputs.size());
            std::vector<uint64_t> batchHashes(count * 4, 0);

            auto t0 = std::chrono::high_resolution_clock::now();
            SHA256BatchWrapper_CPU(inputs, batchHashes.data(), count,
                config.sha256.threads_per_block);
            auto t1 = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            batchDurations.push_back(ms);
            totalDurationMs += ms;

            std::cout << "[INFO] Batch [" << (start / batchSize + 1) << "]"
                << " files=" << count
                << " duration=" << std::fixed << std::setprecision(3) << ms << "ms"
                << " throughput=" << std::setprecision(1)
                << (count / (ms / 1000.0)) << " files/sec"
                << std::endl;
        }

        std::cout << "[INFO] Total duration: " << std::fixed << std::setprecision(3)
            << totalDurationMs << "ms" << std::endl;
        std::cout << "[INFO] Overall throughput: " << std::setprecision(1)
            << (files.size() / (totalDurationMs / 1000.0)) << " files/sec" << std::endl;

        WriteProfileCsv(
            config.output_dir,
            config.sha256.threads_per_block,
            static_cast<uint32_t>(batchSize),
            batchDurations,
            files.size(),
            totalDurationMs);

        return 0;
    }

    // --------------------------------------------------------------------------------
    int AppRunner::RunFFT(const Config& config)
    {
        if (!std::filesystem::exists(config.input_dir))
        {
            std::cerr << "[ERROR] Input directory not found: " << config.input_dir << std::endl;
            return 1;
        }

        auto files = Utils::ScanWavFiles(config.input_dir);
        if (files.empty())
        {
            std::cout << "[INFO] No .wav files found in: " << config.input_dir << std::endl;
            return 0;
        }

        std::cout << "[INFO] FFT mode" << std::endl;
        std::cout << "[INFO] Found " << files.size() << " .wav files." << std::endl;
        std::cout << "[INFO] Batch size: " << config.fft.batch_size << std::endl;
        std::cout << "[INFO] Threads/block: " << config.fft.threads_per_block << std::endl;
        std::cout << "[INFO] FFT size: " << config.fft_size << std::endl;

        // --- Connect to Redis ---
        RedisClient redis;
        bool redisAvailable = redis.Connect();
        if (redisAvailable)
            std::cout << "[INFO] Redis connected - FFT caching enabled." << std::endl;
        else
            std::cout << "[WARN] Redis not available - FFT results won't be cached." << std::endl;

        std::vector<double> durations;
        size_t              skipped = 0;
        size_t              batchSize = config.fft.batch_size;
        double              totalDurationMs = 0.0;
        uint32_t            half = config.fft_size / 2 + 1;

        for (size_t start = 0; start < files.size(); start += batchSize)
        {
            size_t end = std::min(start + batchSize, files.size());
            std::vector<std::vector<uint8_t>> inputs;
            std::vector<std::vector<uint8_t>> survivors;     // PCM data after duplicate filter
            std::vector<std::string>          survivor_hashes; // hashes for survivors only

            for (size_t i = start; i < end; i++)
            {
                WavReader reader(files[i]);
                inputs.push_back(reader.ReadPCM());
            }

            if (inputs.empty()) continue;

            uint32_t count = static_cast<uint32_t>(inputs.size());
            std::vector<uint64_t> batchHashes(count * 4, 0);

            SHA256BatchWrapper_CPU(inputs, batchHashes.data(), count,
                config.sha256.threads_per_block);

            for (uint32_t i = 0; i < count; i++)
            {
                std::string hex = Utils::HashToHex(batchHashes.data() + i * 4);

                if (redisAvailable && redis.MagnitudesExist(hex))
                {
                    skipped++;
                    continue;
                }

                survivors.push_back(std::move(inputs[i]));
                survivor_hashes.push_back(std::move(hex));
            }

            if (survivors.empty()) continue;

            uint32_t survivor_count = static_cast<uint32_t>(survivors.size());
            std::vector<float> magnitudes((uint64_t)survivor_count * half, 0.0f);

            auto t0 = std::chrono::high_resolution_clock::now();
            FFTBatchWrapper_CPU(survivors, magnitudes.data(), survivor_count,
                config.fft_size, config.fft.threads_per_block);
            auto t1 = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            totalDurationMs += ms;

            for (uint32_t i = 0; i < survivor_count; i++)
            {
                durations.push_back(ms / survivor_count);

                if (redisAvailable)
                    redis.SetMagnitudes(
                        survivor_hashes[i],
                        magnitudes.data() + (uint64_t)i * half,
                        half);
            }

            std::cout << "[INFO] Batch [" << (start / batchSize + 1) << "]"
                << " files=" << count
                << " duration=" << std::fixed << std::setprecision(3) << ms << "ms"
                << " throughput=" << std::setprecision(1)
                << (count / (ms / 1000.0)) << " files/sec"
                << std::endl;
        }

        if (skipped > 0)
            std::cout << "[INFO] Skipped " << skipped << " already-processed files." << std::endl;

        std::cout << "[INFO] Total duration: " << std::fixed << std::setprecision(3)
            << totalDurationMs << "ms" << std::endl;
        if (totalDurationMs > 0.0)
            std::cout << "[INFO] Overall throughput: " << std::setprecision(1)
            << (files.size() / (totalDurationMs / 1000.0)) << " files/sec" << std::endl;
        else
            std::cout << "[INFO] Overall throughput: all files served from cache." << std::endl;

        WriteFFTCsv(config.output_dir, durations);
        return 0;
    }

} // namespace SignalForge