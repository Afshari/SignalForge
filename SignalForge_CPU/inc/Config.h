#pragma once
#include <string>
#include <cstdint>
#include <filesystem>

namespace SignalForge {

    struct KernelConfig
    {
        uint32_t batch_size;
        uint32_t threads_per_block;
    };

    struct RedisConfig
    {
        std::string host;
        uint32_t    port;
        uint32_t    db;
    };

    struct Config
    {
        // File settings
        uint64_t                max_file_size_kb;
        uint32_t                sample_rate;

        // Per-kernel settings
        KernelConfig            sha256;
        KernelConfig            fft;
        uint32_t                fft_size;

        uint32_t                reader_threads;
        bool                    verbose;
        RedisConfig             redis;

        // Paths
        std::filesystem::path   test_data_dir;
        std::filesystem::path   output_dir;
        std::filesystem::path   input_dir;

        // Throws std::runtime_error if file not found or invalid
        static Config Load(const std::filesystem::path& filepath);

        // Safe fallback defaults
        static Config Default();
    };

} // namespace SignalForge