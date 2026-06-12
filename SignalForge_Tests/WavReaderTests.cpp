#include "pch.h"
#include "WavReader.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include "TestHelpers.h"

namespace SignalForge {
    // ================================================================================
    // WavReaderTests - Construction & Header Parsing
    // ================================================================================
    TEST(WavReaderTests, ThrowsOnMissingFile)
    {
        EXPECT_THROW(
            SignalForge::WavReader reader(TestHelpers::TestDataPath("nonexistent.wav")),
            std::runtime_error
        );
    }

    TEST(WavReaderTests, ThrowsOnInvalidRiffHeader)
    {
        auto path = TestHelpers::TestDataPath("invalid_riff.wav");
        std::filesystem::create_directories(path.parent_path());

        // Write garbage data
        std::ofstream file(path, std::ios::binary);
        file.write("JUNK\x00\x00\x00\x00WAVE", 12);
        file.close();

        EXPECT_THROW(
            SignalForge::WavReader reader(path),
            std::runtime_error
        );

        std::filesystem::remove(path);
    }

    TEST(WavReaderTests, ThrowsOnNonPcmFormat)
    {
        auto path = TestHelpers::TestDataPath("non_pcm.wav");
        std::filesystem::create_directories(path.parent_path());

        // Write WAV with audioFormat = 3 (IEEE float, not PCM)
        std::ofstream file(path, std::ios::binary);
        uint32_t riffSize = 36;
        uint32_t fmtSize = 16;
        uint16_t audioFormat = 3; // IEEE float
        uint16_t numChannels = 1;
        uint32_t sampleRate = 44100;
        uint32_t byteRate = 176400;
        uint16_t blockAlign = 4;
        uint16_t bitsPerSample = 32;
        uint32_t dataSize = 0;

        file.write("RIFF", 4);
        file.write(reinterpret_cast<const char*>(&riffSize), 4);
        file.write("WAVE", 4);
        file.write("fmt ", 4);
        file.write(reinterpret_cast<const char*>(&fmtSize), 4);
        file.write(reinterpret_cast<const char*>(&audioFormat), 2);
        file.write(reinterpret_cast<const char*>(&numChannels), 2);
        file.write(reinterpret_cast<const char*>(&sampleRate), 4);
        file.write(reinterpret_cast<const char*>(&byteRate), 4);
        file.write(reinterpret_cast<const char*>(&blockAlign), 2);
        file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
        file.write("data", 4);
        file.write(reinterpret_cast<const char*>(&dataSize), 4);
        file.close();

        EXPECT_THROW(
            SignalForge::WavReader reader(path),
            std::runtime_error
        );

        std::filesystem::remove(path);
    }

    // ================================================================================
    // WavReaderTests - Getters
    // ================================================================================
    TEST(WavReaderTests, GetSampleRate_Returns44100)
    {
        auto path = TestHelpers::TestDataPath("test_samplerate.wav");
        std::filesystem::create_directories(path.parent_path());

        std::vector<int16_t> samples(1024, 0);
        TestHelpers::WriteMinimalWav(path, samples, 44100);

        SignalForge::WavReader reader(path);
        EXPECT_EQ(reader.GetSampleRate(), 44100u);

        std::filesystem::remove(path);
    }

    TEST(WavReaderTests, GetNumChannels_ReturnsMono)
    {
        auto path = TestHelpers::TestDataPath("test_channels.wav");
        std::filesystem::create_directories(path.parent_path());

        std::vector<int16_t> samples(1024, 0);
        TestHelpers::WriteMinimalWav(path, samples, 44100, 1);

        SignalForge::WavReader reader(path);
        EXPECT_EQ(reader.GetNumChannels(), 1u);

        std::filesystem::remove(path);
    }

    TEST(WavReaderTests, GetBitDepth_Returns16)
    {
        auto path = TestHelpers::TestDataPath("test_bitdepth.wav");
        std::filesystem::create_directories(path.parent_path());

        std::vector<int16_t> samples(1024, 0);
        TestHelpers::WriteMinimalWav(path, samples);

        SignalForge::WavReader reader(path);
        EXPECT_EQ(reader.GetBitDepth(), 16u);

        std::filesystem::remove(path);
    }

    TEST(WavReaderTests, GetNumSamples_MatchesWrittenSamples)
    {
        auto path = TestHelpers::TestDataPath("test_numsamples.wav");
        std::filesystem::create_directories(path.parent_path());

        std::vector<int16_t> samples(2048, 0);
        TestHelpers::WriteMinimalWav(path, samples);

        SignalForge::WavReader reader(path);
        EXPECT_EQ(reader.GetNumSamples(), 2048u);

        std::filesystem::remove(path);
    }

    // ================================================================================
    // WavReaderTests - ReadPCM
    // ================================================================================
    TEST(WavReaderTests, ReadPCM_ReturnCorrectByteCount)
    {
        auto path = TestHelpers::TestDataPath("test_pcm_size.wav");
        std::filesystem::create_directories(path.parent_path());

        std::vector<int16_t> samples(1024, 0);
        TestHelpers::WriteMinimalWav(path, samples);

        SignalForge::WavReader reader(path);
        auto pcm = reader.ReadPCM();

        // 1024 samples x 2 bytes per sample (16-bit) = 2048 bytes
        EXPECT_EQ(pcm.size(), 2048u);

        std::filesystem::remove(path);
    }

    TEST(WavReaderTests, ReadPCM_ReturnCorrectValues)
    {
        auto path = TestHelpers::TestDataPath("test_pcm_values.wav");
        std::filesystem::create_directories(path.parent_path());

        // Write known sample values
        std::vector<int16_t> samples = { 0, 1000, -1000, 32767, -32767 };
        TestHelpers::WriteMinimalWav(path, samples);

        SignalForge::WavReader reader(path);
        auto pcm = reader.ReadPCM();

        // Verify raw bytes match expected little-endian int16 values
        EXPECT_EQ(pcm.size(), samples.size() * 2);

        for (size_t i = 0; i < samples.size(); i++)
        {
            int16_t reconstructed =
                static_cast<int16_t>(pcm[i * 2] | (pcm[i * 2 + 1] << 8));
            EXPECT_EQ(reconstructed, samples[i]);
        }

        std::filesystem::remove(path);
    }

    TEST(WavReaderTests, ReadPCM_CalledTwiceReturnsSameData)
    {
        auto path = TestHelpers::TestDataPath("test_pcm_twice.wav");
        std::filesystem::create_directories(path.parent_path());

        std::vector<int16_t> samples(512, 42);
        TestHelpers::WriteMinimalWav(path, samples);

        SignalForge::WavReader reader(path);
        auto pcm_a = reader.ReadPCM();
        auto pcm_b = reader.ReadPCM();

        EXPECT_EQ(pcm_a, pcm_b);

        std::filesystem::remove(path);
    }

    // ================================================================================
    // WavReaderTests - Real generated files (from SignalForge_Tools)
    // These tests require generate_signals.py to have been run first
    // ================================================================================
    TEST(WavReaderTests, ReadGeneratedFile_Clean_500kb)
    {
        auto path = TestHelpers::TestDataPath("engine_clean_500kb_00001.wav", "500kb");

        SignalForge::WavReader reader(path);

        EXPECT_EQ(reader.GetSampleRate(), 44100u);
        EXPECT_EQ(reader.GetNumChannels(), 1u);
        EXPECT_EQ(reader.GetBitDepth(), 16u);

        auto pcm = reader.ReadPCM();

        // ~500KB file: PCM data should be ~500KB - 44 bytes header
        EXPECT_GT(pcm.size(), 490u * 1024u);
        EXPECT_LT(pcm.size(), 510u * 1024u);
    }

    TEST(WavReaderTests, DISABLED_ReadGeneratedFile_Noisy_1024kb)
    {
        auto path = TestHelpers::TestDataPath("engine_noisy_1024kb_00001.wav", "1024kb");

        SignalForge::WavReader reader(path);

        EXPECT_EQ(reader.GetSampleRate(), 44100u);
        EXPECT_EQ(reader.GetNumChannels(), 1u);
        EXPECT_EQ(reader.GetBitDepth(), 16u);

        auto pcm = reader.ReadPCM();

        EXPECT_GT(pcm.size(), 1010u * 1024u);
        EXPECT_LT(pcm.size(), 1040u * 1024u);
    }

} // namespace SignalForge