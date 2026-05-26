#include "pch.h"
#include "RedisClient.h"
#include <string>
#include <vector>
#include <cmath>

// --------------------------------------------------------------------------------
// Integration tests - require Redis running on localhost:6379
// --------------------------------------------------------------------------------
static SignalForge::RedisClient MakeClient()
{
    return SignalForge::RedisClient("127.0.0.1", 6379, 1);
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
// Keys are sha256 hex strings, values are ISO 8601 timestamps
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
    std::string hash = "9f0148e8d4556e5cd1fed78881d28d86160ae9606b57129a2b5726d536d3701e";
    std::string timestamp = "2026-05-25T14:32:11";

    EXPECT_TRUE(client.SetHash(hash, timestamp));

    auto result = client.GetHash(hash);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), timestamp);
}

TEST_F(RedisHashTest, GetHash_NonExistentKey_ReturnsNullopt)
{
    std::string hash = "0000000000000000000000000000000000000000000000000000000000000000";
    auto result = client.GetHash(hash);
    EXPECT_FALSE(result.has_value());
}

TEST_F(RedisHashTest, HashExists_ReturnsTrueAfterSet)
{
    std::string hash = "aabbccdd00112233aabbccdd00112233aabbccdd00112233aabbccdd00112233";
    EXPECT_FALSE(client.HashExists(hash));

    client.SetHash(hash, "2026-05-25T14:32:11");
    EXPECT_TRUE(client.HashExists(hash));
}

TEST_F(RedisHashTest, HashExists_ReturnsFalseForUnknownHash)
{
    std::string hash = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    EXPECT_FALSE(client.HashExists(hash));
}

TEST_F(RedisHashTest, SetHash_OverwritesExistingTimestamp)
{
    std::string hash = "9f0148e8d4556e5cd1fed78881d28d86160ae9606b57129a2b5726d536d3701e";
    std::string timestamp1 = "2026-05-25T14:32:11";
    std::string timestamp2 = "2026-05-25T15:00:00";

    client.SetHash(hash, timestamp1);
    client.SetHash(hash, timestamp2);

    auto result = client.GetHash(hash);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), timestamp2);
}

TEST_F(RedisHashTest, SetHash_MultipleHashes_IndependentKeys)
{
    std::string hash_a = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    std::string hash_b = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    client.SetHash(hash_a, "2026-05-25T10:00:00");
    client.SetHash(hash_b, "2026-05-25T11:00:00");

    auto a = client.GetHash(hash_a);
    auto b = client.GetHash(hash_b);

    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(a.value(), "2026-05-25T10:00:00");
    EXPECT_EQ(b.value(), "2026-05-25T11:00:00");
}

// ================================================================================
// RedisClientTests - FFT magnitude operations
// Keys are sha256 hex strings (same hash as used in SetHash for the same file)
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
    std::string hash = "9f0148e8d4556e5cd1fed78881d28d86160ae9606b57129a2b5726d536d3701e";
    std::vector<float> mags = { 0.1f, 0.5f, 1.2f, 0.3f, 0.8f };

    EXPECT_TRUE(client.SetMagnitudes(hash, mags.data(), mags.size()));

    std::vector<float> out;
    EXPECT_TRUE(client.GetMagnitudes(hash, out));

    ASSERT_EQ(out.size(), mags.size());
    for (size_t i = 0; i < mags.size(); i++)
        EXPECT_FLOAT_EQ(out[i], mags[i]);
}

TEST_F(RedisMagnitudeTest, GetMagnitudes_NonExistentKey_ReturnsFalse)
{
    std::string hash = "0000000000000000000000000000000000000000000000000000000000000000";
    std::vector<float> out;
    EXPECT_FALSE(client.GetMagnitudes(hash, out));
}

TEST_F(RedisMagnitudeTest, MagnitudesExist_ReturnsTrueAfterSet)
{
    std::string hash = "aabbccdd00112233aabbccdd00112233aabbccdd00112233aabbccdd00112233";
    std::vector<float> mags = { 1.0f, 2.0f, 3.0f };

    EXPECT_FALSE(client.MagnitudesExist(hash));
    client.SetMagnitudes(hash, mags.data(), mags.size());
    EXPECT_TRUE(client.MagnitudesExist(hash));
}

TEST_F(RedisMagnitudeTest, SetMagnitudes_LargeArray_RoundTrip)
{
    std::string hash = "9f0148e8d4556e5cd1fed78881d28d86160ae9606b57129a2b5726d536d3701e";
    uint32_t size = 65536 / 2 + 1;
    std::vector<float> mags(size);
    for (uint32_t i = 0; i < size; i++)
        mags[i] = (float)i * 0.001f;

    EXPECT_TRUE(client.SetMagnitudes(hash, mags.data(), size));

    std::vector<float> out;
    EXPECT_TRUE(client.GetMagnitudes(hash, out));

    ASSERT_EQ(out.size(), size);
    float max_diff = 0.0f;
    for (uint32_t i = 0; i < size; i++)
        max_diff = std::max(max_diff, std::abs(out[i] - mags[i]));
    EXPECT_LT(max_diff, 1e-6f);
}

TEST_F(RedisMagnitudeTest, HashAndMagnitudes_SameKey_IndependentEntries)
{
    // Verify that sha256: and fft: prefixes keep the two entries separate
    // for the same underlying hash
    std::string hash = "9f0148e8d4556e5cd1fed78881d28d86160ae9606b57129a2b5726d536d3701e";
    std::string timestamp = "2026-05-25T14:32:11";
    std::vector<float> mags = { 0.1f, 0.2f, 0.3f };

    EXPECT_TRUE(client.SetHash(hash, timestamp));
    EXPECT_TRUE(client.SetMagnitudes(hash, mags.data(), mags.size()));

    // Both should exist independently
    EXPECT_TRUE(client.HashExists(hash));
    EXPECT_TRUE(client.MagnitudesExist(hash));

    // Hash entry should not affect magnitude entry
    auto ts = client.GetHash(hash);
    ASSERT_TRUE(ts.has_value());
    EXPECT_EQ(ts.value(), timestamp);

    std::vector<float> out;
    EXPECT_TRUE(client.GetMagnitudes(hash, out));
    ASSERT_EQ(out.size(), mags.size());
}