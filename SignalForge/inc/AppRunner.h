#pragma once
#include "Config.h"
#include "RedisClient.h"
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
        
    };

} // namespace SignalForge