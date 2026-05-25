#pragma once
#include <vector>
#include <cstdint>

namespace SignalForge {

    // Data pushed by the reader thread into m_wav_queue.
    // Contains one batch of raw PCM data ready for GPU processing.
    struct WavBatch
    {
        std::vector<std::vector<uint8_t>> pcm_data;  // raw PCM bytes per file
    };

} // namespace SignalForge