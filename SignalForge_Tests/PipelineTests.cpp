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

    TEST_F(PipelineSha256Test, Pipeline_StoresFftMagSha256InRedis)
    {
        auto files = Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        SignalForge::SignalForgePipeline pipeline(filepaths, config, true);
        pipeline.Run();

        int found = 0;
        uint32_t half = config.fft_size / 2 + 1;
        for (const auto& p : files)
        {
            WavReader reader(p);
            auto pcm = reader.ReadPCM();
            auto mags = ComputeMagnitudes(pcm, config);
            std::string xxhash_hex = MagToXxHash(mags.data(), half);
            if (client.FftMagSha256Exists(xxhash_hex))
                found++;
        }
        EXPECT_GT(found, 0);
    }

    TEST_F(PipelineSha256Test, Pipeline_SecondRun_SkipsAllFiles)
    {
        auto files = Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        SignalForge::SignalForgePipeline pipeline1(filepaths, config, true);
        pipeline1.Run();

        int count_before = 0;
        uint32_t half = config.fft_size / 2 + 1;
        for (const auto& p : files)
        {
            WavReader reader(p);
            auto pcm = reader.ReadPCM();
            auto mags = ComputeMagnitudes(pcm, config);
            std::string xxhash_hex = MagToXxHash(mags.data(), half);
            if (client.FftMagSha256Exists(xxhash_hex))
                count_before++;
        }

        SignalForge::SignalForgePipeline pipeline2(filepaths, config, true);
        pipeline2.Run();

        int count_after = 0;
        for (const auto& p : files)
        {
            WavReader reader(p);
            auto pcm = reader.ReadPCM();
            auto mags = ComputeMagnitudes(pcm, config);
            std::string xxhash_hex = MagToXxHash(mags.data(), half);
            if (client.FftMagSha256Exists(xxhash_hex))
                count_after++;
        }

        EXPECT_EQ(count_before, count_after);
    }

    // ================================================================================
    // RedisClientTests - FftMagSha256 operations
    // Keys are xxHash64 hex strings, prefix fft_mag_sha256:0:
    // ================================================================================

    class RedisFftMagSha256Test : public ::testing::Test
    {
    protected:
        SignalForge::RedisClient client = TestHelpers::MakeClient();

        void SetUp() override
        {
            ASSERT_TRUE(client.Connect());
            client.FlushAll();
        }

        void TearDown() override
        {
            client.FlushAll();
            client.Disconnect();
        }
    };

    TEST_F(RedisFftMagSha256Test, SetFftMagSha256_AndFftMagSha256Exists_RoundTrip)
    {
        std::string xxhash = "a1b2c3d4e5f6a7b8";
        std::vector<float> mags = { 0.1f, 0.5f, 1.2f, 0.3f, 0.8f };

        EXPECT_FALSE(client.FftMagSha256Exists(xxhash));
        EXPECT_TRUE(client.SetFftMagSha256(xxhash, mags.data(), mags.size()));
        EXPECT_TRUE(client.FftMagSha256Exists(xxhash));
    }

    TEST_F(RedisFftMagSha256Test, FftMagSha256Exists_ReturnsFalseForUnknownHash)
    {
        std::string xxhash = "ffffffffffffffff";
        EXPECT_FALSE(client.FftMagSha256Exists(xxhash));
    }

    TEST_F(RedisFftMagSha256Test, FftMagSha256_And_FftMag_SameHash_IndependentEntries)
    {
        // Verify fft_mag_sha256:0: and fft_mag:0: are independent namespaces
        std::string xxhash = "a1b2c3d4e5f6a7b8";
        std::vector<float> mags = { 0.1f, 0.2f, 0.3f };

        client.SetFftMag(xxhash, mags.data(), mags.size());
        EXPECT_TRUE(client.FftMagExists(xxhash));
        EXPECT_FALSE(client.FftMagSha256Exists(xxhash));

        client.SetFftMagSha256(xxhash, mags.data(), mags.size());
        EXPECT_TRUE(client.FftMagExists(xxhash));
        EXPECT_TRUE(client.FftMagSha256Exists(xxhash));
    }

    TEST_F(RedisFftMagSha256Test, SetFftMagSha256_LargeArray_ValuesCorrect)
    {
        std::string xxhash = "a1b2c3d4e5f6a7b8";
        uint32_t size = 8192 / 2 + 1;
        std::vector<float> mags(size);
        for (uint32_t i = 0; i < size; i++)
            mags[i] = (float)i * 0.001f;

        EXPECT_TRUE(client.SetFftMagSha256(xxhash, mags.data(), size));

        std::vector<float> out;
        EXPECT_TRUE(client.GetFftMagSha256(xxhash, out));

        ASSERT_EQ(out.size(), size);
        float max_diff = 0.0f;
        for (uint32_t i = 0; i < size; i++)
            max_diff = std::max(max_diff, std::abs(out[i] - mags[i]));
        EXPECT_LT(max_diff, 1e-6f);
    }

} // namespace SignalForge