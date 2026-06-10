#include "pch.h"
#include "Utils.h"
#include <filesystem>
#include <fstream>
#include <regex>

namespace SignalForge {

    // --------------------------------------------------------------------------------
    // Helpers
    // --------------------------------------------------------------------------------

    // Creates an empty file at the given path.
    static void CreateEmptyFile(const std::filesystem::path& path)
    {
        std::ofstream f(path);
    }

    // Creates a temporary directory under the system temp path.
    // Caller is responsible for removing it after the test.
    static std::filesystem::path MakeTempDir(const std::string& name)
    {
        auto dir = std::filesystem::temp_directory_path() / name;
        std::filesystem::create_directories(dir);
        return dir;
    }

    // --------------------------------------------------------------------------------
    // HashToHex tests
    // --------------------------------------------------------------------------------

    TEST(UtilsHashToHexTest, KnownInput_ProducesCorrectHex)
    {
        // Each uint64_t maps to exactly 16 hex chars, 4 values = 64 chars total.
        uint64_t input[4] = { 0x0123456789ABCDEF,
                              0xFEDCBA9876543210,
                              0x0000000000000001,
                              0xFFFFFFFFFFFFFFFF };

        std::string hex = Utils::HashToHex(input);

        ASSERT_EQ(hex.size(), 64u);
        EXPECT_EQ(hex.substr(0, 16), "0123456789abcdef");
        EXPECT_EQ(hex.substr(16, 16), "fedcba9876543210");
        EXPECT_EQ(hex.substr(32, 16), "0000000000000001");
        EXPECT_EQ(hex.substr(48, 16), "ffffffffffffffff");
    }

    TEST(UtilsHashToHexTest, AllZeros_Produces64Zeros)
    {
        uint64_t input[4] = { 0, 0, 0, 0 };
        std::string hex = Utils::HashToHex(input);

        ASSERT_EQ(hex.size(), 64u);
        EXPECT_EQ(hex, std::string(64, '0'));
    }

    TEST(UtilsHashToHexTest, AllOnes_Produces64Fs)
    {
        uint64_t input[4] = { 0xFFFFFFFFFFFFFFFF,
                              0xFFFFFFFFFFFFFFFF,
                              0xFFFFFFFFFFFFFFFF,
                              0xFFFFFFFFFFFFFFFF };
        std::string hex = Utils::HashToHex(input);

        ASSERT_EQ(hex.size(), 64u);
        EXPECT_EQ(hex, std::string(64, 'f'));
    }

    // --------------------------------------------------------------------------------
    // NowISO8601 tests
    // --------------------------------------------------------------------------------

    TEST(UtilsNowISO8601Test, ReturnsExactly19Characters)
    {
        std::string ts = Utils::NowISO8601();
        EXPECT_EQ(ts.size(), 19u);
    }

    TEST(UtilsNowISO8601Test, MatchesISO8601Format)
    {
        std::string ts = Utils::NowISO8601();
        // Expected format: YYYY-MM-DDTHH:MM:SS
        std::regex pattern(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})");
        EXPECT_TRUE(std::regex_match(ts, pattern))
            << "Timestamp did not match ISO 8601 format: " << ts;
    }

    // --------------------------------------------------------------------------------
    // ScanWavFiles tests
    // --------------------------------------------------------------------------------

    TEST(UtilsScanWavFilesTest, ReturnsOnlyWavFiles)
    {
        auto dir = MakeTempDir("scan_wav_flat");

        CreateEmptyFile(dir / "a.wav");
        CreateEmptyFile(dir / "b.wav");
        CreateEmptyFile(dir / "notes.json");
        CreateEmptyFile(dir / "readme.txt");

        auto result = Utils::ScanWavFiles(dir);

        EXPECT_EQ(result.size(), 2u);
        for (const auto& p : result)
            EXPECT_EQ(p.extension(), ".wav");

        std::filesystem::remove_all(dir);
    }

    TEST(UtilsScanWavFilesTest, NonExistentDirectory_ReturnsEmpty)
    {
        auto dir = std::filesystem::temp_directory_path() / "does_not_exist_xyz";
        auto result = Utils::ScanWavFiles(dir);
        EXPECT_TRUE(result.empty());
    }

    TEST(UtilsScanWavFilesTest, EmptyDirectory_ReturnsEmpty)
    {
        auto dir = MakeTempDir("scan_wav_empty");
        auto result = Utils::ScanWavFiles(dir);
        EXPECT_TRUE(result.empty());
        std::filesystem::remove_all(dir);
    }

} // namespace SignalForge