#include "AppRunner.h"
#include "WavReader.h"
#include "cpu/SignalForge.h"
#include <boost/filesystem.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace SignalForge {

    // --------------------------------------------------------------------------------
    std::vector<std::filesystem::path> AppRunner::ScanWavFiles(
        const std::filesystem::path& dir)
    {
        std::vector<std::filesystem::path> paths;
        if (!std::filesystem::exists(dir))
            return paths;

        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".wav") continue;
            paths.push_back(entry.path());
        }
        return paths;
    }

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
        const std::vector<std::filesystem::path>& files,
        const std::vector<std::string>& hashes,
        const std::vector<double>& durationsMs)
    {
        std::filesystem::create_directories(outputDir);
        auto csvPath = outputDir / "hash_results.csv";

        std::ofstream csv(csvPath);
        csv << "filename,sha256,duration_ms\n";

        for (size_t i = 0; i < files.size(); i++)
            csv << files[i].filename().string() << ","
            << hashes[i] << ","
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
    int AppRunner::RunHash(const Config& config)
    {
        if (!std::filesystem::exists(config.input_dir))
        {
            std::cerr << "[ERROR] Input directory not found: " << config.input_dir << std::endl;
            return 1;
        }

        auto files = ScanWavFiles(config.input_dir);
        if (files.empty())
        {
            std::cout << "[INFO] No .wav files found in: " << config.input_dir << std::endl;
            return 0;
        }

        std::cout << "[INFO] Found " << files.size() << " .wav files." << std::endl;

        std::vector<std::string> hashes;
        std::vector<double>      durations;

        // Process in batches
        size_t batchSize = config.batch_size;
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
            SHA256BatchWrapper_CPU(inputs, batchHashes.data(), count, config.threads_per_block);
            auto t1 = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            for (uint32_t i = 0; i < count; i++)
            {
                hashes.push_back(HashToHex(batchHashes.data() + i * 4));
                durations.push_back(ms / count);
            }
        }

        WriteResultsCsv(config.output_dir, files, hashes, durations);
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

        auto files = ScanWavFiles(config.input_dir);
        if (files.empty())
        {
            std::cout << "[INFO] No .wav files found in: " << config.input_dir << std::endl;
            return 0;
        }

        std::cout << "[INFO] Profile mode" << std::endl;
        std::cout << "[INFO] Found " << files.size() << " .wav files." << std::endl;
        std::cout << "[INFO] Batch size: " << config.batch_size << std::endl;
        std::cout << "[INFO] Threads/block: " << config.threads_per_block << std::endl;

        std::vector<double> batchDurations;
        double              totalDurationMs = 0.0;
        size_t              batchSize = config.batch_size;

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
            SHA256BatchWrapper_CPU(inputs, batchHashes.data(), count, config.threads_per_block);
            auto t1 = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            batchDurations.push_back(ms);
            totalDurationMs += ms;

            std::cout << "[INFO] Batch [" << (start / batchSize + 1) << "]"
                << " files=" << count
                << " duration=" << std::fixed << std::setprecision(3) << ms << "ms"
                << " throughput=" << std::setprecision(1) << (count / (ms / 1000.0)) << " files/sec"
                << std::endl;
        }

        std::cout << "[INFO] Total duration: " << std::fixed << std::setprecision(3)
            << totalDurationMs << "ms" << std::endl;
        std::cout << "[INFO] Overall throughput: " << std::setprecision(1)
            << (files.size() / (totalDurationMs / 1000.0)) << " files/sec" << std::endl;

        WriteProfileCsv(
            config.output_dir,
            config.threads_per_block,
            static_cast<uint32_t>(batchSize),
            batchDurations,
            files.size(),
            totalDurationMs
        );

        return 0;
    }

} // namespace SignalForge