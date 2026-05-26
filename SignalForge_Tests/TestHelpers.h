#pragma once
#include <filesystem>
#include <vector>
#include <fstream>
#include <cstdint>
#include "WavReader.h"
#include "RedisClient.h"
#include "cpu/SignalForge.h"

namespace SignalForge::TestHelpers {

    static std::filesystem::path TestDataPath(const std::string& filename)
    {
        return std::filesystem::current_path() / "test_data" / filename;
    }

    // --------------------------------------------------------------------------------
    static std::filesystem::path ConfigPath()
    {
        return std::filesystem::current_path() / "config.json";
    }

    // --------------------------------------------------------------------------------
    static SignalForge::RedisClient MakeClient()
    {
        return SignalForge::RedisClient("127.0.0.1", 6379, 1);
    }

    // --------------------------------------------------------------------------------
    static std::vector<float> FFTWavFile(
        const std::filesystem::path& path,
        uint32_t fft_size = 65536,
        uint32_t threads_per_block = 128)
    {
        SignalForge::WavReader reader(path);
        auto pcm = reader.ReadPCM();

        std::vector<std::vector<uint8_t>> inputs = { pcm };
        uint32_t half = fft_size / 2 + 1;
        std::vector<float> magnitudes(half, 0.0f);

        FFTBatchWrapper_CPU(inputs, magnitudes.data(), 1, fft_size, threads_per_block);
        return magnitudes;
    }

    // --------------------------------------------------------------------------------
    static void HashWavFile(const std::filesystem::path& path, uint64_t* h_hash)
    {
        SignalForge::WavReader reader(path);
        auto pcm = reader.ReadPCM();
        SHA256HashWrapper_CPU(pcm.data(), pcm.size(), h_hash);
    }

    // --------------------------------------------------------------------------------
    // Helper: write a minimal valid PCM WAV file for testing
    // mono, 16-bit, 44100 Hz
    // --------------------------------------------------------------------------------
    static void WriteMinimalWav(
        const std::filesystem::path& path,
        const std::vector<int16_t>& samples,
        uint32_t                     sampleRate = 44100,
        uint16_t                     numChannels = 1,
        uint16_t                     bitsPerSample = 16)
    {
        uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
        uint32_t riffSize = 36 + dataSize;
        uint16_t audioFormat = 1; // PCM
        uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
        uint16_t blockAlign = numChannels * (bitsPerSample / 8);

        std::ofstream file(path, std::ios::binary);

        // RIFF chunk
        file.write("RIFF", 4);
        file.write(reinterpret_cast<const char*>(&riffSize), 4);
        file.write("WAVE", 4);

        // fmt chunk
        file.write("fmt ", 4);
        uint32_t fmtSize = 16;
        file.write(reinterpret_cast<const char*>(&fmtSize), 4);
        file.write(reinterpret_cast<const char*>(&audioFormat), 2);
        file.write(reinterpret_cast<const char*>(&numChannels), 2);
        file.write(reinterpret_cast<const char*>(&sampleRate), 4);
        file.write(reinterpret_cast<const char*>(&byteRate), 4);
        file.write(reinterpret_cast<const char*>(&blockAlign), 2);
        file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

        // data chunk
        file.write("data", 4);
        file.write(reinterpret_cast<const char*>(&dataSize), 4);
        file.write(reinterpret_cast<const char*>(samples.data()), dataSize);
    }

} // namespace SignalForge::TestHelpers