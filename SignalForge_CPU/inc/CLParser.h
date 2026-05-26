#pragma once
#include "Config.h"
#include <filesystem>
#include <string>

namespace SignalForge {

    class CLParser
    {
    public:
        static CLParser Parse(int argc, char* argv[]);
        static Config   LoadConfig(const std::filesystem::path& configDir);

        bool IsProfileMode()  const  { return m_profileMode; }
        bool IsFftMode()      const  { return m_fftMode; }
        bool IsPipelineMode() const  { return m_pipelineMode; }
        const std::filesystem::path& GetConfigDir() const { return m_configDir; }

    private:
        bool                  m_profileMode = false;
        bool                  m_fftMode = false;
        bool                  m_pipelineMode = false;
        std::filesystem::path m_configDir;
    };

} // namespace SignalForge