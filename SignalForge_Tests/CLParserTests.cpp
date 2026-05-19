#include "pch.h"
#include "CLParser.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class CLParserTest : public ::testing::Test
{
protected:
    fs::path temp_dir;

    void SetUp() override
    {
        temp_dir = fs::temp_directory_path() / "CLParserTests";
        fs::create_directories(temp_dir);
    }

    void TearDown() override
    {
        fs::remove_all(temp_dir);
    }

    void CreateConfig()
    {
        std::ofstream f(temp_dir / "config.json");
        f << "{\n"
            << "  \"file\":  { \"max_file_size_kb\": 2048, \"sample_rate\": 44100 },\n"
            << "  \"batch\": { \"batch_size\": 10 },\n"
            << "  \"gpu\":   { \"threads_per_block\": 32, \"fft_size\": 65536 },\n"
            << "  \"paths\": {\n"
            << "    \"test_data_dir\": \"test_data\",\n"
            << "    \"output_dir\":    \"output\",\n"
            << "    \"input_dir\":     \"input\"\n"
            << "  }\n"
            << "}\n";
    }
};

// ================================================================================
// CLParserTests - Argument parsing
// ================================================================================

// Test 1: No args - defaults to current path, hash mode
TEST_F(CLParserTest, NoArgs_DefaultsToExePathAndHashMode)
{
    // Simulate real executable path with full path
    char* argv[] = { (char*)"D:\\some\\path\\SignalForge.exe" };
    auto parser = SignalForge::CLParser::Parse(1, argv);

    EXPECT_FALSE(parser.IsProfileMode());
    EXPECT_EQ(parser.GetConfigDir(), fs::path("D:\\some\\path"));
}

TEST_F(CLParserTest, NoArgs_NoDirectory_ConfigDirIsEmpty)
{
    char* argv[] = { (char*)"SignalForge.exe" };
    auto parser = SignalForge::CLParser::Parse(1, argv);

    EXPECT_FALSE(parser.IsProfileMode());
    EXPECT_TRUE(parser.GetConfigDir().empty());
}

// Test 2: --profile flag sets profile mode
TEST_F(CLParserTest, ProfileFlag_SetsProfileMode)
{
    char* argv[] = { (char*)"SignalForge.exe", (char*)"--profile" };
    auto parser = SignalForge::CLParser::Parse(2, argv);

    EXPECT_TRUE(parser.IsProfileMode());
}

// Test 3: --config dir is parsed correctly
TEST_F(CLParserTest, ConfigDirArg_ParsedCorrectly)
{
    std::string dir = temp_dir.string();
    char* argv[] = { (char*)"SignalForge.exe", (char*)"--config", (char*)dir.c_str() };
    auto parser = SignalForge::CLParser::Parse(3, argv);

    EXPECT_FALSE(parser.IsProfileMode());
    EXPECT_EQ(parser.GetConfigDir(), fs::path(dir));
}

// Test 4: Both --config and --profile parsed correctly
TEST_F(CLParserTest, ConfigDirAndProfileFlag_BothParsedCorrectly)
{
    std::string dir = temp_dir.string();
    char* argv[] = { (char*)"SignalForge.exe", (char*)"--config", (char*)dir.c_str(), (char*)"--profile" };
    auto parser = SignalForge::CLParser::Parse(4, argv);

    EXPECT_TRUE(parser.IsProfileMode());
    EXPECT_EQ(parser.GetConfigDir(), fs::path(dir));
}

// Test 5: --profile before --config — order doesn't matter
TEST_F(CLParserTest, ProfileFlagBeforeConfigDir_OrderDoesNotMatter)
{
    std::string dir = temp_dir.string();
    char* argv[] = { (char*)"SignalForge.exe", (char*)"--profile", (char*)"--config", (char*)dir.c_str() };
    auto parser = SignalForge::CLParser::Parse(4, argv);

    EXPECT_TRUE(parser.IsProfileMode());
    EXPECT_EQ(parser.GetConfigDir(), fs::path(dir));
}

// Test 6: Unknown argument does not crash — just warns
TEST_F(CLParserTest, UnknownArg_DoesNotCrash)
{
    char* argv[] = { (char*)"SignalForge.exe", (char*)"--unknown" };
    EXPECT_NO_THROW(SignalForge::CLParser::Parse(2, argv));
}

// ================================================================================
// CLParserTests - LoadConfig
// ================================================================================

// Test 7: Missing config.json falls back to defaults
TEST_F(CLParserTest, MissingConfigJson_ReturnsDefaults)
{
    auto config = SignalForge::CLParser::LoadConfig(temp_dir);

    EXPECT_EQ(config.threads_per_block, 32u);
    EXPECT_EQ(config.sample_rate, 44100u);
    EXPECT_EQ(config.batch_size, 10u);
}

// Test 8: Valid config.json loads correctly
TEST_F(CLParserTest, ValidConfigJson_LoadsCorrectly)
{
    CreateConfig();
    auto config = SignalForge::CLParser::LoadConfig(temp_dir);

    EXPECT_EQ(config.max_file_size_kb, 2048u);
    EXPECT_EQ(config.sample_rate, 44100u);
    EXPECT_EQ(config.batch_size, 10u);
    EXPECT_EQ(config.threads_per_block, 32u);
    EXPECT_EQ(config.fft_size, 65536u);
}

// Test 9: Config loaded from correct directory
TEST_F(CLParserTest, LoadConfig_ReadsFromCorrectDirectory)
{
    CreateConfig();
    auto config = SignalForge::CLParser::LoadConfig(temp_dir);

    EXPECT_EQ(config.test_data_dir, fs::weakly_canonical(temp_dir / "test_data"));
    EXPECT_EQ(config.output_dir, fs::weakly_canonical(temp_dir / "output"));
    EXPECT_EQ(config.input_dir, fs::weakly_canonical(temp_dir / "input"));
}