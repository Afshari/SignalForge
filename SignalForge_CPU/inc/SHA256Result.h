#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace SignalForge {
    struct SHA256Result
    {
        std::vector<std::string> hashes;
        uint32_t count;
    };
}