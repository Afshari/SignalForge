#include "pch.h"
#include "RedisClient.h"
#include "Config.h"
#include "Utils.h"
#include "WavReader.h"
#include "TestHelpers.h"
#include "SignalForgePipeline.h"
#include "cpu/SignalForge.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <xxhash.h>
#include <set>

namespace SignalForge {

    // --------------------------------------------------------------------------------
    // Helpers
    // --------------------------------------------------------------------------------

    // Computes xxHash64 of a magnitude array and returns 16-char hex string.
    static std::string MagToXxHash(const float* data, uint32_t count)
    {
        uint64_t hash64 = XXH64(data, (size_t)count * sizeof(float), 0);
        std::ostringstream oss;
        oss << std::hex << std::setw(16) << std::setfill('0') << hash64;
        return oss.str();
    }

    // Runs FFT on a single PCM buffer and returns the magnitude array.
    static std::vector<float> ComputeMagnitudes(
        const std::vector<uint8_t>& pcm,
        const Config& config)
    {
        uint32_t half = config.fft_size / 2 + 1;
        std::vector<std::vector<uint8_t>> inputs = { pcm };
        std::vector<float> magnitudes(half, 0.0f);
        FFTBatchWrapper_CPU(inputs, magnitudes.data(), 1,
            config.fft_size, config.fft.threads_per_block);
        return magnitudes;
    }

    // ================================================================================
    // PipelineTest - FFT-only pipeline (default)
    // ================================================================================

    class PipelineTest : public ::testing::Test
    {
    protected:
        SignalForge::RedisClient client = TestHelpers::MakeClient();
        SignalForge::Config config;

        void SetUp() override
        {
            ASSERT_TRUE(client.Connect());
            client.FlushAll();
            config = SignalForge::Config::Load(
                std::filesystem::current_path() / "config.json");
            config.test_data_dir = std::filesystem::current_path() / "test_data" / "500kb";
            config.input_dir = std::filesystem::current_path() / "test_data" / "500kb";
            config.output_dir = std::filesystem::current_path() / "output";
            config.redis.db = 1;
        }

        void TearDown() override
        {
            client.FlushAll();
            client.Disconnect();
        }
    };

    TEST_F(PipelineTest, Pipeline_RunsWithoutCrashing)
    {
        auto files = Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        SignalForge::SignalForgePipeline pipeline(filepaths, config);
        EXPECT_NO_THROW(pipeline.Run());
    }

    TEST_F(PipelineTest, Pipeline_StoresFftMagInRedis)
    {
        auto files = Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        SignalForge::SignalForgePipeline pipeline(filepaths, config);
        pipeline.Run();

        int found = 0;
        uint32_t half = config.fft_size / 2 + 1;
        for (const auto& p : files)
        {
            WavReader reader(p);
            auto pcm = reader.ReadPCM();
            auto mags = ComputeMagnitudes(pcm, config);
            std::string xxhash_hex = MagToXxHash(mags.data(), half);
            if (client.FftMagExists(xxhash_hex))
                found++;
        }
        EXPECT_GT(found, 0);
    }

    TEST_F(PipelineTest, Pipeline_SecondRun_SkipsFftDuplicates)
    {
        auto files = Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        // First run - populate Redis
        SignalForge::SignalForgePipeline pipeline1(filepaths, config);
        pipeline1.Run();

        int count_before = 0;
        uint32_t half = config.fft_size / 2 + 1;
        for (const auto& p : files)
        {
            WavReader reader(p);
            auto pcm = reader.ReadPCM();
            auto mags = ComputeMagnitudes(pcm, config);
            std::string xxhash_hex = MagToXxHash(mags.data(), half);
            if (client.FftMagExists(xxhash_hex))
                count_before++;
        }

        // Second run - all should be skipped
        SignalForge::SignalForgePipeline pipeline2(filepaths, config);
        pipeline2.Run();

        int count_after = 0;
        for (const auto& p : files)
        {
            WavReader reader(p);
            auto pcm = reader.ReadPCM();
            auto mags = ComputeMagnitudes(pcm, config);
            std::string xxhash_hex = MagToXxHash(mags.data(), half);
            if (client.FftMagExists(xxhash_hex))
                count_after++;
        }

        EXPECT_EQ(count_before, count_after);
    }

    TEST_F(PipelineTest, Pipeline_NoSha256KeysWritten)
    {
        auto files = Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        SignalForge::SignalForgePipeline pipeline(filepaths, config);
        pipeline.Run();

        // FFT-only pipeline must not write any sha256: keys
        int found = 0;
        for (const auto& p : files)
        {
            WavReader reader(p);
            auto pcm = reader.ReadPCM();
            std::vector<std::vector<uint8_t>> inputs = { pcm };
            std::vector<uint64_t> hash(4, 0);
            SHA256BatchWrapper_CPU(inputs, hash.data(), 1,
                config.sha256.threads_per_block);
            std::string hex = Utils::HashToHex(hash.data());
            if (client.HashExists(hex))
                found++;
        }
        EXPECT_EQ(found, 0);
    }

    // ================================================================================
    // PipelineSha256Test - SHA-256 pipeline (sha256_mode=true)
    // ================================================================================

    class PipelineSha256Test : public ::testing::Test
    {
    protected:
        SignalForge::RedisClient client = TestHelpers::MakeClient();
        SignalForge::Config config;

        void SetUp() override
        {
            ASSERT_TRUE(client.Connect());
            client.FlushAll();
            config = SignalForge::Config::Load(
                std::filesystem::current_path() / "config.json");
            config.test_data_dir = std::filesystem::current_path() / "test_data" / "500kb";
            config.input_dir = std::filesystem::current_path() / "test_data" / "500kb";
            config.output_dir = std::filesystem::current_path() / "output";
            config.redis.db = 1;
        }

        void TearDown() override
        {
            client.FlushAll();
            client.Disconnect();
        }
    };

    TEST_F(PipelineSha256Test, Pipeline_RunsWithoutCrashing)
    {
        auto files = Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        SignalForge::SignalForgePipeline pipeline(filepaths, config, true);
        EXPECT_NO_THROW(pipeline.Run());
    }

    TEST_F(PipelineSha256Test, Pipeline_StoresHashesInRedis)
    {
        auto files = Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        SignalForge::SignalForgePipeline pipeline(filepaths, config, true);
        pipeline.Run();

        int found = 0;
        for (const auto& p : files)
        {
            WavReader reader(p);
            auto pcm = reader.ReadPCM();
            std::vector<std::vector<uint8_t>> inputs = { pcm };
            std::vector<uint64_t> hash(4, 0);
            SHA256BatchWrapper_CPU(inputs, hash.data(), 1,
                config.sha256.threads_per_block);
            std::string hex = Utils::HashToHex(hash.data());
            if (client.HashExists(hex))
                found++;
        }
        EXPECT_GT(found, 0);
    }

    TEST_F(PipelineSha256Test, Pipeline_SecondRun_SkipsAllHashes)
    {
        auto files = Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        SignalForge::SignalForgePipeline pipeline1(filepaths, config, true);
        pipeline1.Run();

        int count_before = 0;
        for (const auto& p : files)
        {
            WavReader reader(p);
            auto pcm = reader.ReadPCM();
            std::vector<std::vector<uint8_t>> inputs = { pcm };
            std::vector<uint64_t> hash(4, 0);
            SHA256BatchWrapper_CPU(inputs, hash.data(), 1,
                config.sha256.threads_per_block);
            std::string hex = Utils::HashToHex(hash.data());
            if (client.HashExists(hex))
                count_before++;
        }

        SignalForge::SignalForgePipeline pipeline2(filepaths, config, true);
        pipeline2.Run();

        int count_after = 0;
        for (const auto& p : files)
        {
            WavReader reader(p);
            auto pcm = reader.ReadPCM();
            std::vector<std::vector<uint8_t>> inputs = { pcm };
            std::vector<uint64_t> hash(4, 0);
            SHA256BatchWrapper_CPU(inputs, hash.data(), 1,
                config.sha256.threads_per_block);
            std::string hex = Utils::HashToHex(hash.data());
            if (client.HashExists(hex))
                count_after++;
        }

        EXPECT_EQ(count_before, count_after);
    }

    TEST_F(PipelineTest, Pipeline_MixedDuplicatesAndUniques_InSameBatch)
    {
        auto files = Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        SignalForge::SignalForgePipeline pipeline(filepaths, config);
        pipeline.Run();

        // Every file's xxHash should exist in Redis, whether unique or duplicate
        uint32_t half = config.fft_size / 2 + 1;
        std::set<std::string> seen_hashes;

        for (const auto& p : files)
        {
            WavReader reader(p);
            auto pcm = reader.ReadPCM();
            auto mags = ComputeMagnitudes(pcm, config);
            std::string xxhash_hex = MagToXxHash(mags.data(), half);

            EXPECT_TRUE(client.FftMagExists(xxhash_hex));
            seen_hashes.insert(xxhash_hex);
        }

        // Confirm there ARE duplicates in this dataset (clean files collapse)
        EXPECT_LT(seen_hashes.size(), files.size());
    }
} // namespace SignalForge