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
        bool IsHashMode()     const { return m_hashMode; }
        bool IsPipelineMode() const  { return m_pipelineMode; }
        bool IsGrpcMode()     const  { return m_grpcMode; }
        bool IsPipelineSha256Mode() const { return m_pipelineSha256Mode; }
        const std::filesystem::path& GetConfigDir() const { return m_configDir; }

    private:
        bool                  m_profileMode = false;
        bool                  m_fftMode = false;
        bool                  m_hashMode = false;
        bool                  m_pipelineMode = false;
        bool                  m_grpcMode = false;
        bool                  m_pipelineSha256Mode = false;
        std::filesystem::path m_configDir;
    };

} // namespace SignalForge