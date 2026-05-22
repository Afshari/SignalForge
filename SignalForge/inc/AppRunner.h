#pragma once
#include "Config.h"
#include <string>
#include <vector>

namespace SignalForge {

    class AppRunner
    {
    public:
        static int RunHash(const Config& config);
        static int RunProfile(const Config& config);
        static int RunFFT(const Config& config);

    private:
        static std::vector<std::filesystem::path> ScanWavFiles(
            const std::filesystem::path& dir);
    };

} // namespace SignalForge