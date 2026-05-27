#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace SignalForge {

    // Data pushed by the GPU worker thread into m_result_queue.
    // Contains one batch of processed results ready for Redis storage.
    struct FFTResult
    {
        std::vector<std::string> hashes;      // sha256 hex string per file
        std::vector<float>       magnitudes;  // flat array, half floats per file
        uint32_t                 count;       // number of files in this batch
        uint32_t                 half;        // fft_size/2+1, needed to index per file
    };

} // namespace SignalForge