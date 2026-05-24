#include "pch.h"
#include "RedisClient.h"
#include <string>
#include <vector>
#include <cmath>

// --------------------------------------------------------------------------------
// Helper: create a connected client
// Requires Redis running on localhost:6379
// --------------------------------------------------------------------------------
static SignalForge::RedisClient MakeClient()
{
    return SignalForge::RedisClient("127.0.0.1", 6379);
}

// ================================================================================
// RedisClientTests - Connection
// ================================================================================

TEST(RedisClientTests, Connect_Succeeds)
{
    auto client = MakeClient();
    EXPECT_TRUE(client.Connect());
    EXPECT_TRUE(client.IsConnected());
}

TEST(RedisClientTests, Ping_ReturnsTrue)
{
    auto client = MakeClient();
    ASSERT_TRUE(client.Connect());
    EXPECT_TRUE(client.Ping());
}

TEST(RedisClientTests, Disconnect_SetsNotConnected)
{
    auto client = MakeClient();
    ASSERT_TRUE(client.Connect());
    client.Disconnect();
    EXPECT_FALSE(client.IsConnected());
}

TEST(RedisClientTests, Connect_WrongPort_Fails)
{
    SignalForge::RedisClient client("127.0.0.1", 9999);
    EXPECT_FALSE(client.Connect());
    EXPECT_FALSE(client.IsConnected());
}

// ================================================================================
// RedisClientTests - SHA-256 hash operations
// ================================================================================

class RedisHashTest : public ::testing::Test
{
protected:
    SignalForge::RedisClient client;

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

TEST_F(RedisHashTest, SetHash_AndGetHash_RoundTrip)
{
    std::string key = "engine_clean_500kb_001.wav";
    std::string hash = "9f0148e8d4556e5cd1fed78881d28d86160ae9606b57129a2b5726d536d3701e";

    EXPECT_TRUE(client.SetHash(key, hash));

    auto result = client.GetHash(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), hash);
}

TEST_F(RedisHashTest, GetHash_NonExistentKey_ReturnsNullopt)
{
    auto result = client.GetHash("nonexistent.wav");
    EXPECT_FALSE(result.has_value());
}

TEST_F(RedisHashTest, HashExists_ReturnsTrueAfterSet)
{
    std::string key = "engine_noisy_500kb_001.wav";
    EXPECT_FALSE(client.HashExists(key));

    client.SetHash(key, "aabbccdd");
    EXPECT_TRUE(client.HashExists(key));
}

TEST_F(RedisHashTest, HashExists_ReturnsFalseForUnknownKey)
{
    EXPECT_FALSE(client.HashExists("unknown_file.wav"));
}

TEST_F(RedisHashTest, SetHash_OverwritesExistingValue)
{
    std::string key = "engine_clean_500kb_001.wav";
    std::string hash1 = "aabbccdd";
    std::string hash2 = "11223344";

    client.SetHash(key, hash1);
    client.SetHash(key, hash2);

    auto result = client.GetHash(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), hash2);
}

TEST_F(RedisHashTest, SetHash_MultipleFiles_IndependentKeys)
{
    client.SetHash("file_a.wav", "hash_a");
    client.SetHash("file_b.wav", "hash_b");

    auto a = client.GetHash("file_a.wav");
    auto b = client.GetHash("file_b.wav");

    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(a.value(), "hash_a");
    EXPECT_EQ(b.value(), "hash_b");
}

// ================================================================================
// RedisClientTests - FFT magnitude operations
// ================================================================================

class RedisMagnitudeTest : public ::testing::Test
{
protected:
    SignalForge::RedisClient client;

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

TEST_F(RedisMagnitudeTest, SetMagnitudes_AndGetMagnitudes_RoundTrip)
{
    std::string key = "engine_clean_500kb_001.wav";
    std::vector<float> mags = { 0.1f, 0.5f, 1.2f, 0.3f, 0.8f };

    EXPECT_TRUE(client.SetMagnitudes(key, mags.data(), mags.size()));

    std::vector<float> out;
    EXPECT_TRUE(client.GetMagnitudes(key, out));

    ASSERT_EQ(out.size(), mags.size());
    for (size_t i = 0; i < mags.size(); i++)
        EXPECT_FLOAT_EQ(out[i], mags[i]);
}

TEST_F(RedisMagnitudeTest, GetMagnitudes_NonExistentKey_ReturnsFalse)
{
    std::vector<float> out;
    EXPECT_FALSE(client.GetMagnitudes("nonexistent.wav", out));
}

TEST_F(RedisMagnitudeTest, MagnitudesExist_ReturnsTrueAfterSet)
{
    std::string key = "engine_noisy_500kb_001.wav";
    std::vector<float> mags = { 1.0f, 2.0f, 3.0f };

    EXPECT_FALSE(client.MagnitudesExist(key));
    client.SetMagnitudes(key, mags.data(), mags.size());
    EXPECT_TRUE(client.MagnitudesExist(key));
}

TEST_F(RedisMagnitudeTest, SetMagnitudes_LargeArray_RoundTrip)
{
    std::string key = "engine_clean_1024kb_001.wav";
    uint32_t size = 65536 / 2 + 1;
    std::vector<float> mags(size);
    for (uint32_t i = 0; i < size; i++)
        mags[i] = (float)i * 0.001f;

    EXPECT_TRUE(client.SetMagnitudes(key, mags.data(), size));

    std::vector<float> out;
    EXPECT_TRUE(client.GetMagnitudes(key, out));

    ASSERT_EQ(out.size(), size);
    float max_diff = 0.0f;
    for (uint32_t i = 0; i < size; i++)
        max_diff = std::max(max_diff, std::abs(out[i] - mags[i]));
    EXPECT_LT(max_diff, 1e-6f);
}