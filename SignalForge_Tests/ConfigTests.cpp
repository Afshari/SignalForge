#include "pch.h"
#include "Config.h"
#include <filesystem>
#include <fstream>
#include "TestHelpers.h"

namespace SignalForge {

    // ================================================================================
    // ConfigTests - Load from file
    // ================================================================================
    TEST(ConfigTests, LoadConfig_ValidFile)
    {
        auto config = SignalForge::Config::Load(TestHelpers::ConfigPath());

        EXPECT_EQ(config.max_file_size_kb, 2048u);
        EXPECT_EQ(config.sample_rate, 44100u);
        EXPECT_EQ(config.sha256.batch_size, 5120u);
        EXPECT_EQ(config.sha256.threads_per_block, 128u);
        EXPECT_EQ(config.fft.batch_size, 1024u);
        EXPECT_EQ(config.fft.threads_per_block, 256u);
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
        auto path = std::filesystem::current_path() / "config_broken.json";

        std::ofstream file(path);
        file << R"({
        "file": { "max_file_size_kb": 2048, "sample_rate": 44100 },
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
        EXPECT_EQ(config.sha256.batch_size, 5120u);
        EXPECT_EQ(config.sha256.threads_per_block, 128u);
        EXPECT_EQ(config.fft.batch_size, 1024u);
        EXPECT_EQ(config.fft.threads_per_block, 256u);
        EXPECT_EQ(config.fft_size, 65536u);
        EXPECT_EQ(config.test_data_dir, std::filesystem::path("test_data"));
        EXPECT_EQ(config.output_dir, std::filesystem::path("output"));
        EXPECT_EQ(config.input_dir, std::filesystem::path("input"));
    }

    TEST(ConfigTests, DefaultConfig_ThreadsPerBlock_IsNonZero)
    {
        auto config = SignalForge::Config::Default();
        EXPECT_GT(config.sha256.threads_per_block, 0u);
        EXPECT_GT(config.fft.threads_per_block, 0u);
    }

    TEST(ConfigTests, DefaultConfig_FftSize_IsPowerOfTwo)
    {
        auto config = SignalForge::Config::Default();
        EXPECT_GT(config.fft_size, 0u);
        EXPECT_EQ(config.fft_size & (config.fft_size - 1), 0u);
    }

    TEST(ConfigTests, DefaultConfig_MaxFileSizeKb_WithinExpectedRange)
    {
        auto config = SignalForge::Config::Default();
        EXPECT_GE(config.max_file_size_kb, 500u);
        EXPECT_LE(config.max_file_size_kb, 2048u);
    }

    // ================================================================================
    // ConfigTests - Load matches Default
    // ================================================================================
    TEST(ConfigTests, LoadConfig_MatchesDefault)
    {
        auto loaded = SignalForge::Config::Load(TestHelpers::ConfigPath());
        auto def = SignalForge::Config::Default();

        EXPECT_EQ(loaded.max_file_size_kb, def.max_file_size_kb);
        EXPECT_EQ(loaded.sample_rate, def.sample_rate);
        EXPECT_EQ(loaded.sha256.batch_size, def.sha256.batch_size);
        EXPECT_EQ(loaded.sha256.threads_per_block, def.sha256.threads_per_block);
        EXPECT_EQ(loaded.fft.batch_size, def.fft.batch_size);
        EXPECT_EQ(loaded.fft.threads_per_block, def.fft.threads_per_block);
        EXPECT_EQ(loaded.fft_size, def.fft_size);
        EXPECT_EQ(loaded.test_data_dir, def.test_data_dir);
        EXPECT_EQ(loaded.output_dir, def.output_dir);
        EXPECT_EQ(loaded.input_dir, def.input_dir);
    }


    // ================================================================================
    // ConfigTests - ReaderThreads
    // ================================================================================
    TEST(ConfigTest, Config_DefaultReaderThreads)
    {
        auto config = SignalForge::Config::Default();
        EXPECT_EQ(config.reader_threads, 1);
    }

    TEST(ConfigTest, Config_LoadReaderThreads)
    {
        // write a temp config with pipeline section
        auto tmp = std::filesystem::temp_directory_path() / "test_config_readers.json";
        std::ofstream f(tmp);
        f << R"({
        "file": { "max_file_size_kb": 2048, "sample_rate": 44100 },
        "kernels": {
            "sha256": { "batch_size": 5120, "threads_per_block": 128 },
            "fft": { "batch_size": 1024, "threads_per_block": 256, "fft_size": 65536 }
        },
        "paths": { "test_data_dir": "test_data", "output_dir": "output", "input_dir": "input" },
        "pipeline": { "reader_threads": 4 }
    })";
        f.close();

        auto config = SignalForge::Config::Load(tmp);
        EXPECT_EQ(config.reader_threads, 4);
        std::filesystem::remove(tmp);
    }

    TEST(ConfigTest, Config_LoadMissingPipelineSection_DefaultsToOne)
    {
        auto tmp = std::filesystem::temp_directory_path() / "test_config_no_pipeline.json";
        std::ofstream f(tmp);
        f << R"({
        "file": { "max_file_size_kb": 2048, "sample_rate": 44100 },
        "kernels": {
            "sha256": { "batch_size": 5120, "threads_per_block": 128 },
            "fft": { "batch_size": 1024, "threads_per_block": 256, "fft_size": 65536 }
        },
        "paths": { "test_data_dir": "test_data", "output_dir": "output", "input_dir": "input" }
    })";
        f.close();

        auto config = SignalForge::Config::Load(tmp);
        EXPECT_EQ(config.reader_threads, 1);
        std::filesystem::remove(tmp);
    }

} // namespace SignalForge