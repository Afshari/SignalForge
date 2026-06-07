#include "WavReader.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace SignalForge {

    // --------------------------------------------------------------------------------
    // WAV header structure (standard 44-byte PCM)
    // --------------------------------------------------------------------------------
    struct WavHeader
    {
        // RIFF chunk
        char     riffId[4];       // "RIFF"
        uint32_t riffSize;        // file size - 8
        char     waveId[4];       // "WAVE"

        // fmt chunk
        char     fmtId[4];        // "fmt "
        uint32_t fmtSize;         // 16 for PCM
        uint16_t audioFormat;     // 1 = PCM
        uint16_t numChannels;
        uint32_t sampleRate;
        uint32_t byteRate;
        uint16_t blockAlign;
        uint16_t bitsPerSample;

        // data chunk
        char     dataId[4];       // "data"
        uint32_t dataSize;
    };

    // --------------------------------------------------------------------------------
    WavReader::WavReader(const std::filesystem::path& filePath)
        : m_filePath(filePath)
    {
        ParseHeader();
    }

    // --------------------------------------------------------------------------------
    void WavReader::ParseHeader()
    {
        if (!std::filesystem::exists(m_filePath))
            throw std::runtime_error(
                "WavReader: file not found: " + m_filePath.string());

        std::ifstream file(m_filePath, std::ios::binary);
        if (!file.is_open())
            throw std::runtime_error(
                "WavReader: failed to open file: " + m_filePath.string());

        // --- Read RIFF header ---
        char riffId[4];
        file.read(riffId, 4);
        if (std::strncmp(riffId, "RIFF", 4) != 0)
            throw std::runtime_error(
                "WavReader: not a valid WAV file (missing RIFF): " + m_filePath.string());

        uint32_t riffSize;
        file.read(reinterpret_cast<char*>(&riffSize), 4);

        char waveId[4];
        file.read(waveId, 4);
        if (std::strncmp(waveId, "WAVE", 4) != 0)
            throw std::runtime_error(
                "WavReader: not a valid WAV file (missing WAVE): " + m_filePath.string());

        // --- Search for fmt chunk ---
        // Handles both standard 44-byte and extended headers
        bool fmtFound = false;
        bool dataFound = false;

        while (file && (!fmtFound || !dataFound))
        {
            char chunkId[4];
            uint32_t chunkSize;

            if (!file.read(chunkId, 4)) break;
            if (!file.read(reinterpret_cast<char*>(&chunkSize), 4)) break;

            if (std::strncmp(chunkId, "fmt ", 4) == 0)
            {
                uint16_t audioFormat;
                file.read(reinterpret_cast<char*>(&audioFormat), 2);
                if (audioFormat != 1)
                    throw std::runtime_error(
                        "WavReader: only PCM (format 1) is supported: " + m_filePath.string());

                file.read(reinterpret_cast<char*>(&m_numChannels), 2);
                file.read(reinterpret_cast<char*>(&m_sampleRate), 4);

                uint32_t byteRate;
                file.read(reinterpret_cast<char*>(&byteRate), 4);

                uint16_t blockAlign;
                file.read(reinterpret_cast<char*>(&blockAlign), 2);

                file.read(reinterpret_cast<char*>(&m_bitDepth), 2);

                // Skip any extra fmt bytes (extended header)
                if (chunkSize > 16)
                    file.seekg(chunkSize - 16, std::ios::cur);

                fmtFound = true;
            }
            else if (std::strncmp(chunkId, "data", 4) == 0)
            {
                m_dataSize = chunkSize;
                m_dataOffset = static_cast<uint32_t>(file.tellg());
                dataFound = true;
            }
            else
            {
                // Skip unknown chunks
                file.seekg(chunkSize, std::ios::cur);
            }
        }

        if (!fmtFound)
            throw std::runtime_error(
                "WavReader: fmt chunk not found: " + m_filePath.string());

        if (!dataFound)
            throw std::runtime_error(
                "WavReader: data chunk not found: " + m_filePath.string());

        // --- Compute number of samples ---
        uint32_t bytesPerSample = m_bitDepth / 8;
        m_numSamples = m_dataSize / (bytesPerSample * m_numChannels);
    }

    // --------------------------------------------------------------------------------
    std::vector<uint8_t> WavReader::ReadPCM() const
    {
#ifndef _WIN32
        int fd = open(m_filePath.c_str(), O_RDONLY);
        if (fd == -1)
            throw std::runtime_error(
                "WavReader: failed to open file for reading: " + m_filePath.string());

        posix_fadvise(fd, m_dataOffset, m_dataSize, POSIX_FADV_SEQUENTIAL);

        struct stat sb;
        fstat(fd, &sb);

        void* mapped = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED)
        {
            close(fd);
            throw std::runtime_error(
                "WavReader: mmap failed: " + m_filePath.string());
        }

        std::vector<uint8_t> pcm(m_dataSize);
        std::memcpy(pcm.data(),
            static_cast<uint8_t*>(mapped) + m_dataOffset,
            m_dataSize);

        munmap(mapped, sb.st_size);
        close(fd);
#else
        std::ifstream file(m_filePath, std::ios::binary);
        if (!file.is_open())
            throw std::runtime_error(
                "WavReader: failed to open file for reading: " + m_filePath.string());

        file.seekg(m_dataOffset, std::ios::beg);

        std::vector<uint8_t> pcm(m_dataSize);
        file.read(reinterpret_cast<char*>(pcm.data()), m_dataSize);

        if (!file)
            throw std::runtime_error(
                "WavReader: failed to read PCM data: " + m_filePath.string());
#endif
        return pcm;
    }

    // --------------------------------------------------------------------------------
    uint32_t WavReader::GetSampleRate()  const { return m_sampleRate; }
    uint32_t WavReader::GetNumSamples()  const { return m_numSamples; }
    uint16_t WavReader::GetNumChannels() const { return m_numChannels; }
    uint16_t WavReader::GetBitDepth()    const { return m_bitDepth; }

} // namespace SignalForge