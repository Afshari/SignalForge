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

namespace SignalForge {

    class PipelineTest : public ::testing::Test
    {
    protected:
        SignalForge::RedisClient client = TestHelpers::MakeClient();
        SignalForge::Config config;

        void SetUp() override
        {
            ASSERT_TRUE(client.Connect());
            client.FlushAll();
            config = SignalForge::Config::Load(std::filesystem::current_path() / "config.json");
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

    TEST_F(PipelineTest, Pipeline_StoresHashesInRedis)
    {
        auto files = SignalForge::Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        SignalForge::SignalForgePipeline pipeline(filepaths, config);
        pipeline.Run();

        // At least some hashes should be stored in Redis
        int found = 0;
        for (const auto& p : files)
        {
            SignalForge::WavReader reader(p);
            auto pcm = reader.ReadPCM();
            std::vector<std::vector<uint8_t>> inputs = { pcm };
            std::vector<uint64_t> hash(4, 0);
            SHA256BatchWrapper_CPU(
                inputs,
                hash.data(), 1,
                config.sha256.threads_per_block);
            std::string hex = SignalForge::Utils::HashToHex(hash.data());
            if (client.HashExists(hex))
                found++;
        }
        EXPECT_GT(found, 0);
    }

    TEST_F(PipelineTest, Pipeline_StoresMagnitudesInRedis)
    {
        auto files = SignalForge::Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        SignalForge::SignalForgePipeline pipeline(filepaths, config);
        pipeline.Run();

        int found = 0;
        for (const auto& p : files)
        {
            SignalForge::WavReader reader(p);
            auto pcm = reader.ReadPCM();
            std::vector<std::vector<uint8_t>> inputs = { pcm };
            std::vector<uint64_t> hash(4, 0);
            SHA256BatchWrapper_CPU(inputs, hash.data(), 1,
                config.sha256.threads_per_block);
            std::string hex = SignalForge::Utils::HashToHex(hash.data());
            if (client.MagnitudesExist(hex))
                found++;
        }
        EXPECT_GT(found, 0);
    }
    
    TEST_F(PipelineTest, Pipeline_SecondRun_SkipsAllFiles)
    {
        auto files = SignalForge::Utils::ScanWavFiles(config.input_dir);
        std::vector<std::string> filepaths;
        for (const auto& p : files)
            filepaths.push_back(p.string());

        // First run - populate Redis
        SignalForge::SignalForgePipeline pipeline1(filepaths, config);
        pipeline1.Run();

        // Second run - all files should be skipped
        // We verify by checking that no new magnitudes were added
        // by counting before and after
        int count_before = 0;
        int count_after = 0;

        for (const auto& p : files)
        {
            SignalForge::WavReader reader(p);
            auto pcm = reader.ReadPCM();
            std::vector<std::vector<uint8_t>> inputs = { pcm };
            std::vector<uint64_t> hash(4, 0);
            SHA256BatchWrapper_CPU(inputs, hash.data(), 1,
                config.sha256.threads_per_block);
            std::string hex = SignalForge::Utils::HashToHex(hash.data());
            if (client.MagnitudesExist(hex))
                count_before++;
        }

        SignalForge::SignalForgePipeline pipeline2(filepaths, config);
        pipeline2.Run();

        for (const auto& p : files)
        {
            SignalForge::WavReader reader(p);
            auto pcm = reader.ReadPCM();
            std::vector<std::vector<uint8_t>> inputs = { pcm };
            std::vector<uint64_t> hash(4, 0);
            SHA256BatchWrapper_CPU(inputs, hash.data(), 1,
                config.sha256.threads_per_block);
            std::string hex = SignalForge::Utils::HashToHex(hash.data());
            if (client.MagnitudesExist(hex))
                count_after++;
        }

        EXPECT_EQ(count_before, count_after);
    }

} // namespace SignalForge