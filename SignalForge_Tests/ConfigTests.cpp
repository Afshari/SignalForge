#include "pch.h"
#include "Config.h"
#include <filesystem>
#include <fstream>

// --------------------------------------------------------------------------------
// Helper: build path to config.json relative to the test executable
// --------------------------------------------------------------------------------
static std::filesystem::path ConfigPath()
{
    return std::filesystem::current_path() / "config.json";
}

// ================================================================================
// ConfigTests - Load from file
// ================================================================================
TEST(ConfigTests, LoadConfig_ValidFile)
{
    auto config = SignalForge::Config::Load(ConfigPath());

    EXPECT_EQ(config.max_file_size_kb, 2048u);
    EXPECT_EQ(config.sample_rate, 44100u);
    EXPECT_EQ(config.batch_size, 10u);
    EXPECT_EQ(config.threads_per_block, 32u);
    EXPECT_EQ(config.fft_size, 65536u);
    EXPECT_EQ(config.test_data_dir, std::filesystem::path("test_data"));
    EXPECT_EQ(config.output_dir, std::filesystem::path("output"));
    EXPECT_EQ(config.input_dir, std::filesystem::path("input"));
}

TEST(ConfigTests, LoadConfig_FileNotFound)
{
    EXPECT_THROW(
        SignalForge::Config::Load("nonexistent.json"),
        std::runtime_error
    );
}

TEST(ConfigTests, LoadConfig_ThrowsOnMissingKey)
{
    // Write a broken config file missing the gpu section
    auto path = std::filesystem::current_path() / "config_broken.json";

    std::ofstream file(path);
    file << R"({
        "file": { "max_file_size_kb": 2048, "sample_rate": 44100 },
        "batch": { "batch_size": 10 },
        "paths": {
            "test_data_dir": "test_data",
            "output_dir": "output",
            "input_dir": "input"
        }
    })";
    file.close();

    EXPECT_THROW(
        SignalForge::Config::Load(path),
        std::exception
    );

    std::filesystem::remove(path);
}

// ================================================================================
// ConfigTests - Default values
// ================================================================================
TEST(ConfigTests, DefaultConfig_ValuesAreValid)
{
    auto config = SignalForge::Config::Default();

    EXPECT_EQ(config.max_file_size_kb, 2048u);
    EXPECT_EQ(config.sample_rate, 44100u);
    EXPECT_EQ(config.batch_size, 10u);
    EXPECT_EQ(config.threads_per_block, 32u);
    EXPECT_EQ(config.fft_size, 65536u);
    EXPECT_EQ(config.test_data_dir, std::filesystem::path("test_data"));
    EXPECT_EQ(config.output_dir, std::filesystem::path("output"));
    EXPECT_EQ(config.input_dir, std::filesystem::path("input"));
}

TEST(ConfigTests, DefaultConfig_ThreadsPerBlock_IsNonZero)
{
    auto config = SignalForge::Config::Default();
    EXPECT_GT(config.threads_per_block, 0u);
}

TEST(ConfigTests, DefaultConfig_FftSize_IsPowerOfTwo)
{
    auto config = SignalForge::Config::Default();
    // FFT size must be power of two for cuFFT
    EXPECT_GT(config.fft_size, 0u);
    EXPECT_EQ(config.fft_size & (config.fft_size - 1), 0u);
}

TEST(ConfigTests, DefaultConfig_MaxFileSizeKb_WithinExpectedRange)
{
    auto config = SignalForge::Config::Default();
    // Should be between 500KB and 2048KB for SignalForge medium file target
    EXPECT_GE(config.max_file_size_kb, 500u);
    EXPECT_LE(config.max_file_size_kb, 2048u);
}

// ================================================================================
// ConfigTests - Load matches Default
// ================================================================================
TEST(ConfigTests, LoadConfig_MatchesDefault)
{
    auto loaded = SignalForge::Config::Load(ConfigPath());
    auto def = SignalForge::Config::Default();

    EXPECT_EQ(loaded.max_file_size_kb, def.max_file_size_kb);
    EXPECT_EQ(loaded.sample_rate, def.sample_rate);
    EXPECT_EQ(loaded.batch_size, def.batch_size);
    EXPECT_EQ(loaded.threads_per_block, def.threads_per_block);
    EXPECT_EQ(loaded.fft_size, def.fft_size);
    EXPECT_EQ(loaded.test_data_dir, def.test_data_dir);
    EXPECT_EQ(loaded.output_dir, def.output_dir);
    EXPECT_EQ(loaded.input_dir, def.input_dir);
}