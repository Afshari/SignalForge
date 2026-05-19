#pragma once
#include <string>
#include <cstdint>
#include <filesystem>

namespace SignalForge {

    struct Config
    {
        // File settings
        uint64_t                max_file_size_kb;
        uint32_t                sample_rate;

        // Batch settings
        uint32_t                batch_size;

        // GPU settings
        uint32_t                threads_per_block;
        uint32_t                fft_size;

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