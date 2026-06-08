#include "pch.h"
#include "Config.h"
#include <filesystem>
#include <fstream>
#include "TestHelpers.h"

namespace SignalForge {

	// ================================================================================
	// helpers
	// ================================================================================
	static std::filesystem::path WriteTempConfig(const std::string& filename, const std::string& json)
	{
		auto path = std::filesystem::temp_directory_path() / filename;
		std::ofstream f(path);
		f << json;
		f.close();
		return path;
	}

	static const std::string k_minimal_config = R"({
		"file": { "max_file_size_kb": 2048, "sample_rate": 44100 },
		"kernels": {
			"sha256": { "batch_size": 1024, "threads_per_block": 64 },
			"fft": { "batch_size": 1024, "threads_per_block": 256, "fft_size": 65536 }
		},
		"paths": { "test_data_dir": "test_data", "output_dir": "output", "input_dir": "input" },
		"redis": { "host": "127.0.0.1", "port": 6379, "db": 1 }
	})";

	// ================================================================================
	// Load from file
	// ================================================================================
	TEST(ConfigTests, LoadConfig_ValidFile)
	{
		auto config = SignalForge::Config::Load(TestHelpers::ConfigPath());

		EXPECT_EQ(config.max_file_size_kb, 2048u);
		EXPECT_EQ(config.sample_rate, 44100u);
		EXPECT_EQ(config.sha256.batch_size, 1024u);
		EXPECT_EQ(config.sha256.threads_per_block, 64u);
		EXPECT_EQ(config.fft.batch_size, 4096u);
		EXPECT_EQ(config.fft.threads_per_block, 256u);
		EXPECT_EQ(config.fft_size, 8192u);
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
		auto path = WriteTempConfig("config_broken.json", R"({
            "file": { "max_file_size_kb": 2048, "sample_rate": 44100 },
            "paths": {
                "test_data_dir": "test_data",
                "output_dir": "output",
                "input_dir": "input"
            }
        })");

		EXPECT_THROW(SignalForge::Config::Load(path), std::exception);
		std::filesystem::remove(path);
	}

	// ================================================================================
	// Default values
	// ================================================================================
	TEST(ConfigTests, DefaultConfig_ValuesAreValid)
	{
		auto config = SignalForge::Config::Default();

		EXPECT_EQ(config.max_file_size_kb, 2048u);
		EXPECT_EQ(config.sample_rate, 44100u);
		EXPECT_EQ(config.sha256.batch_size, 1024u);
		EXPECT_EQ(config.sha256.threads_per_block, 64u);
		EXPECT_EQ(config.fft.batch_size, 4096u);
		EXPECT_EQ(config.fft.threads_per_block, 256u);
		EXPECT_EQ(config.fft_size, 8192u);
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
	// Load matches Default
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
	// reader_threads
	// ================================================================================
	TEST(ConfigTests, DefaultConfig_ReaderThreads_IsOne)
	{
		auto config = SignalForge::Config::Default();
		EXPECT_EQ(config.reader_threads, 4u);
	}

	TEST(ConfigTests, LoadConfig_ReaderThreads_FromPipelineSection)
	{
		auto path = WriteTempConfig("test_config_readers.json",
			k_minimal_config.substr(0, k_minimal_config.rfind('}')) + R"(,
            "pipeline": { "reader_threads": 4 }
        })");

		auto config = SignalForge::Config::Load(path);
		EXPECT_EQ(config.reader_threads, 4u);
		std::filesystem::remove(path);
	}

	TEST(ConfigTests, LoadConfig_MissingPipelineSection_DefaultsToOne)
	{
		auto path = WriteTempConfig("test_config_no_pipeline.json", k_minimal_config);

		auto config = SignalForge::Config::Load(path);
		EXPECT_EQ(config.reader_threads, 1u);
		std::filesystem::remove(path);
	}

	// ================================================================================
	// verbose
	// ================================================================================
	TEST(ConfigTests, DefaultConfig_Verbose_IsFalse)
	{
		auto config = SignalForge::Config::Default();
		EXPECT_FALSE(config.verbose);
	}

	TEST(ConfigTests, LoadConfig_Verbose_True)
	{
		auto path = WriteTempConfig("test_config_verbose.json",
			k_minimal_config.substr(0, k_minimal_config.rfind('}')) + R"(,
            "verbose": true
        })");

		auto config = SignalForge::Config::Load(path);
		EXPECT_TRUE(config.verbose);
		std::filesystem::remove(path);
	}

	TEST(ConfigTests, LoadConfig_Verbose_False)
	{
		auto path = WriteTempConfig("test_config_verbose_false.json",
			k_minimal_config.substr(0, k_minimal_config.rfind('}')) + R"(,
            "verbose": false
        })");

		auto config = SignalForge::Config::Load(path);
		EXPECT_FALSE(config.verbose);
		std::filesystem::remove(path);
	}

	TEST(ConfigTests, LoadConfig_MissingVerbose_DefaultsFalse)
	{
		auto path = WriteTempConfig("test_config_no_verbose.json", k_minimal_config);

		auto config = SignalForge::Config::Load(path);
		EXPECT_FALSE(config.verbose);
		std::filesystem::remove(path);
	}

	// ================================================================================
	// redis
	// ================================================================================
	TEST(ConfigTests, DefaultConfig_Redis_IsLocalhost)
	{
		auto config = SignalForge::Config::Default();
		EXPECT_EQ(config.redis.host, "127.0.0.1");
		EXPECT_EQ(config.redis.port, 6379u);
	}

	TEST(ConfigTests, LoadConfig_Redis_FromFile)
	{
		auto config = SignalForge::Config::Load(TestHelpers::ConfigPath());
		EXPECT_FALSE(config.redis.host.empty());
		EXPECT_GT(config.redis.port, 0u);
	}

	TEST(ConfigTests, LoadConfig_MissingRedisSection_DefaultsToLocalhost)
	{
		auto path = WriteTempConfig("test_config_no_redis.json", k_minimal_config);
		auto config = SignalForge::Config::Load(path);
		EXPECT_EQ(config.redis.host, "127.0.0.1");
		EXPECT_EQ(config.redis.port, 6379u);
		std::filesystem::remove(path);
	}

} // namespace SignalForge