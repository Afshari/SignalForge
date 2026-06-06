#include "pch.h"
#include "CLParser.h"
#include <filesystem>
#include <fstream>

namespace SignalForge {

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
                << "  \"file\": { \"max_file_size_kb\": 2048, \"sample_rate\": 44100 },\n"
                << "  \"kernels\": {\n"
                << "    \"sha256\": { \"batch_size\": 512, \"threads_per_block\": 128 },\n"
                << "    \"fft\":    { \"batch_size\": 4096, \"threads_per_block\": 256, \"fft_size\": 8192 }\n"
                << "  },\n"
                << "  \"paths\": {\n"
                << "    \"test_data_dir\": \"test_data\",\n"
                << "    \"output_dir\":    \"output\",\n"
                << "    \"input_dir\":     \"input\"\n"
                << "  },\n"
                << "  \"redis\": { \"host\": \"127.0.0.1\", \"port\": 6379, \"db\": 1 }\n"
                << "}\n";
        }
    };

    // ================================================================================
    // CLParserTests - Argument parsing
    // ================================================================================

    // Test 1: No args - defaults to current path, hash mode
    TEST_F(CLParserTest, NoArgs_DefaultsToExePathAndHashMode)
    {
#ifdef _WIN32
        char* argv[] = { (char*)"D:\\some\\path\\SignalForge.exe" };
        auto parser = SignalForge::CLParser::Parse(1, argv);
        EXPECT_EQ(parser.GetConfigDir(), fs::path("D:\\some\\path"));
#else
        char* argv[] = { (char*)"/usr/local/bin/SignalForge" };
        auto parser = SignalForge::CLParser::Parse(1, argv);
        EXPECT_EQ(parser.GetConfigDir(), fs::path("/usr/local/bin"));
#endif

        EXPECT_FALSE(parser.IsProfileMode());
        EXPECT_FALSE(parser.IsProfileMode());
        EXPECT_FALSE(parser.IsFftMode());
        EXPECT_FALSE(parser.IsPipelineMode());
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

    // Test 5: --profile before --config order doesn't matter
    TEST_F(CLParserTest, ProfileFlagBeforeConfigDir_OrderDoesNotMatter)
    {
        std::string dir = temp_dir.string();
        char* argv[] = { (char*)"SignalForge.exe", (char*)"--profile", (char*)"--config", (char*)dir.c_str() };
        auto parser = SignalForge::CLParser::Parse(4, argv);

        EXPECT_TRUE(parser.IsProfileMode());
        EXPECT_EQ(parser.GetConfigDir(), fs::path(dir));
    }

    // Test 6: Unknown argument does not crash just warns
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

        EXPECT_EQ(config.sha256.threads_per_block, 128u);
        EXPECT_EQ(config.sha256.batch_size, 512u);
        EXPECT_EQ(config.sample_rate, 44100u);
    }

    // Test 8: Valid config.json loads correctly
    TEST_F(CLParserTest, ValidConfigJson_LoadsCorrectly)
    {
        CreateConfig();
        auto config = SignalForge::CLParser::LoadConfig(temp_dir);

        EXPECT_EQ(config.max_file_size_kb, 2048u);
        EXPECT_EQ(config.sample_rate, 44100u);
        EXPECT_EQ(config.sha256.batch_size, 512u);
        EXPECT_EQ(config.sha256.threads_per_block, 128u);
        EXPECT_EQ(config.fft.batch_size, 4096u);
        EXPECT_EQ(config.fft.threads_per_block, 256u);
        EXPECT_EQ(config.fft_size, 8192u);
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

    // ================================================================================
    // CLParserTests - FFT mode
    // ================================================================================

    TEST_F(CLParserTest, FftFlag_SetsFftMode)
    {
        char* argv[] = { (char*)"SignalForge.exe", (char*)"--fft" };
        auto parser = SignalForge::CLParser::Parse(2, argv);

        EXPECT_TRUE(parser.IsFftMode());
        EXPECT_FALSE(parser.IsProfileMode());
    }

    TEST_F(CLParserTest, FftFlag_AndConfigDir_BothParsedCorrectly)
    {
        std::string dir = temp_dir.string();
        char* argv[] = { (char*)"SignalForge.exe", (char*)"--fft", (char*)"--config", (char*)dir.c_str() };
        auto parser = SignalForge::CLParser::Parse(4, argv);

        EXPECT_TRUE(parser.IsFftMode());
        EXPECT_FALSE(parser.IsProfileMode());
        EXPECT_EQ(parser.GetConfigDir(), fs::path(dir));
    }

    TEST_F(CLParserTest, NoFftFlag_FftModeIsFalse)
    {
        char* argv[] = { (char*)"SignalForge.exe" };
        auto parser = SignalForge::CLParser::Parse(1, argv);

        EXPECT_FALSE(parser.IsFftMode());
    }

    TEST_F(CLParserTest, ProfileAndFft_AreIndependent)
    {
        char* argv[] = { (char*)"SignalForge.exe", (char*)"--profile", (char*)"--fft" };
        auto parser = SignalForge::CLParser::Parse(3, argv);

        EXPECT_TRUE(parser.IsProfileMode());
        EXPECT_TRUE(parser.IsFftMode());
    }

    // ================================================================================
    // CLParserTests - Pipeline mode
    // ================================================================================

    TEST_F(CLParserTest, PipelineFlag_SetsPipelineMode)
    {
        char* argv[] = { (char*)"SignalForge.exe", (char*)"--pipeline" };
        auto parser = SignalForge::CLParser::Parse(2, argv);

        EXPECT_FALSE(parser.IsFftMode());
        EXPECT_FALSE(parser.IsProfileMode());
        EXPECT_TRUE(parser.IsPipelineMode());
    }

    TEST_F(CLParserTest, PipelineFlag_AndConfigDir_BothParsedCorrectly)
    {
        std::string dir = temp_dir.string();
        char* argv[] = { (char*)"SignalForge.exe", (char*)"--pipeline", (char*)"--config", (char*)dir.c_str() };
        auto parser = SignalForge::CLParser::Parse(4, argv);

        EXPECT_FALSE(parser.IsFftMode());
        EXPECT_FALSE(parser.IsProfileMode());
        EXPECT_TRUE(parser.IsPipelineMode());
        EXPECT_EQ(parser.GetConfigDir(), fs::path(dir));
    }

    TEST_F(CLParserTest, NoPipelineFlag_PipelineModeIsFalse)
    {
        char* argv[] = { (char*)"SignalForge.exe" };
        auto parser = SignalForge::CLParser::Parse(1, argv);

        EXPECT_FALSE(parser.IsPipelineMode());
    }

    TEST_F(CLParserTest, ProfileAndPipeline_AreIndependent)
    {
        char* argv[] = { (char*)"SignalForge.exe", (char*)"--profile", (char*)"--pipeline" };
        auto parser = SignalForge::CLParser::Parse(3, argv);

        EXPECT_TRUE(parser.IsProfileMode());
        EXPECT_TRUE(parser.IsPipelineMode());
    }

    // ================================================================================
    // CLParserTests - Hash mode (default)
    // ================================================================================

    TEST_F(CLParserTest, NoFlags_DefaultsToHashMode)
    {
        char* argv[] = { (char*)"SignalForge.exe" };
        auto parser = SignalForge::CLParser::Parse(1, argv);

        EXPECT_FALSE(parser.IsProfileMode());
        EXPECT_FALSE(parser.IsFftMode());
        EXPECT_FALSE(parser.IsPipelineMode());
    }

    TEST_F(CLParserTest, HashMode_IsIndependentOfOtherFlags)
    {
        char* argv[] = { (char*)"SignalForge.exe", (char*)"--profile",
                         (char*)"--fft", (char*)"--pipeline" };
        auto parser = SignalForge::CLParser::Parse(4, argv);

        EXPECT_TRUE(parser.IsProfileMode());
        EXPECT_TRUE(parser.IsFftMode());
        EXPECT_TRUE(parser.IsPipelineMode());
    }

} // namespace SignalForge