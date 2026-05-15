#include "pch.h"
#include "cpu\SignalForge.h"
#include <filesystem>

TEST(SHA256Tests, AllZeros_64bytes)
{
    uint8_t  h_input[64] = { 0 };
    uint64_t h_hash[4] = { 0, 0, 0, 0 };

    SHA256HashWrapper_CPU(h_input, 64, h_hash);

    // Python: hashlib.sha256(bytes(64)).hexdigest()
    // = f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b
    EXPECT_EQ(h_hash[0], 0xF5A5FD42D16A2030ULL);
    EXPECT_EQ(h_hash[1], 0x2798EF6ED309979BULL);
    EXPECT_EQ(h_hash[2], 0x43003D2320D9F0E8ULL);
    EXPECT_EQ(h_hash[3], 0xEA9831A92759FB4BULL);
}

TEST(SHA256Tests, AllZeros_256bytes)
{
    uint8_t  h_input[256] = { 0 };
    uint64_t h_hash[4] = { 0, 0, 0, 0 };

    SHA256HashWrapper_CPU(h_input, 256, h_hash);

    // Python: hashlib.sha256(bytes(256)).hexdigest()
    // = 5341e6b2646979a70e57653007a1f310169421ec9bdd9f1a5648f75ade005af1
    EXPECT_EQ(h_hash[0], 0x5341E6B2646979A7ULL);
    EXPECT_EQ(h_hash[1], 0x0E57653007A1F310ULL);
    EXPECT_EQ(h_hash[2], 0x169421EC9BDD9F1AULL);
    EXPECT_EQ(h_hash[3], 0x5648F75ADE005AF1ULL);
}

TEST(SHA256Tests, AllZeros_1024bytes)
{
    uint8_t  h_input[1024] = { 0 };
    uint64_t h_hash[4] = { 0, 0, 0, 0 };

    SHA256HashWrapper_CPU(h_input, 1024, h_hash);

    // Python: hashlib.sha256(bytes(1024)).hexdigest()
    // = 5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef
    EXPECT_EQ(h_hash[0], 0x5F70BF18A0860070ULL);
    EXPECT_EQ(h_hash[1], 0x16E948B04AED3B82ULL);
    EXPECT_EQ(h_hash[2], 0x103A36BEA41755B6ULL);
    EXPECT_EQ(h_hash[3], 0xCDDFAF10ACE3C6EFULL);
}

TEST(SHA256Tests, Sequential_1024bytes)
{
    uint8_t h_input[1024];
    for (int i = 0; i < 1024; i++)
        h_input[i] = i % 256;
    uint64_t h_hash[4] = { 0, 0, 0, 0 };

    SHA256HashWrapper_CPU(h_input, 1024, h_hash);

    // Python: hashlib.sha256(bytes(range(256)) * 4).hexdigest()
    // = 785b0751fc2c53dc14a4ce3d800e69ef9ce1009eb327ccf458afe09c242c26c9
    EXPECT_EQ(h_hash[0], 0x785B0751FC2C53DCULL);
    EXPECT_EQ(h_hash[1], 0x14A4CE3D800E69EFULL);
    EXPECT_EQ(h_hash[2], 0x9CE1009EB327CCF4ULL);
    EXPECT_EQ(h_hash[3], 0x58AFE09C242C26C9ULL);
}

TEST(SHA256Tests, Alternating_1024bytes)
{
    uint8_t h_input[1024];
    for (int i = 0; i < 1024; i++)
        h_input[i] = (i % 2 == 0) ? 0xAA : 0x55;
    uint64_t h_hash[4] = { 0, 0, 0, 0 };

    SHA256HashWrapper_CPU(h_input, 1024, h_hash);

    // Python: hashlib.sha256(bytes([0xAA, 0x55] * 512)).hexdigest()
    // = d4576a496a9c31503534882c0b7d4cc82afbcc67d59e8a3485eed782b653ae7f
    EXPECT_EQ(h_hash[0], 0xD4576A496A9C3150ULL);
    EXPECT_EQ(h_hash[1], 0x3534882C0B7D4CC8ULL);
    EXPECT_EQ(h_hash[2], 0x2AFBCC67D59E8A34ULL);
    EXPECT_EQ(h_hash[3], 0x85EED782B653AE7FULL);
}

TEST(SHA256Tests, AllFF_1024bytes)
{
    uint8_t h_input[1024];
    memset(h_input, 0xFF, 1024);
    uint64_t h_hash[4] = { 0, 0, 0, 0 };

    SHA256HashWrapper_CPU(h_input, 1024, h_hash);

    // Python: hashlib.sha256(bytes([0xFF] * 1024)).hexdigest()
    // = 5f4ecdb7b71c3e403983fe405cddcdc2f2576b655fdb3e80d94a6f7c32e58bc2
    EXPECT_EQ(h_hash[0], 0x5F4ECDB7B71C3E40ULL);
    EXPECT_EQ(h_hash[1], 0x3983FE405CDDCDC2ULL);
    EXPECT_EQ(h_hash[2], 0xF2576B655FDB3E80ULL);
    EXPECT_EQ(h_hash[3], 0xD94A6F7C32E58BC2ULL);
}

TEST(SHA256Tests, DISABLED_ReadFromFile_1024bytes)
{
}

TEST(FileIOTests, DISABLED_ReadFile_TextFile)
{
}

TEST(SHA256Tests, DISABLED_ReadFromFile_TextFile_62bytes)
{
}

TEST(SHA256Tests, DISABLED_Sequential_100bytes)
{
}

TEST(SHA256Tests, DISABLED_AllAB_500bytes)
{
}

TEST(SHA256Tests, DISABLED_All7F_1000bytes)
{
}

TEST(SHA256Tests, DISABLED_BatchHash_MatchesSingleHash)
{
}

TEST(SHA256Tests, DISABLED_BatchHash_KnownValues)
{
}