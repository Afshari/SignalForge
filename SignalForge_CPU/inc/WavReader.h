#pragma once
#include <vector>
#include <string>
#include <filesystem>

namespace SignalForge {

    class WavReader
    {
    public:
        explicit WavReader(const std::filesystem::path& filePath);

        // Read raw PCM bytes (skips WAV header)
        std::vector<uint8_t>    ReadPCM()       const;
        uint32_t                GetSampleRate() const;
        uint32_t                GetNumSamples() const;
        uint16_t                GetNumChannels() const;
        uint16_t                GetBitDepth()   const;

    private:
        std::filesystem::path   m_filePath;
        uint32_t                m_sampleRate = 0;
        uint32_t                m_numSamples = 0;
        uint16_t                m_numChannels = 0;
        uint16_t                m_bitDepth = 0;
        uint32_t                m_dataOffset = 0;  // byte offset where PCM data starts
        uint32_t                m_dataSize = 0;  // size of PCM data in bytes

        void ParseHeader();
    };

} // namespace SignalForge