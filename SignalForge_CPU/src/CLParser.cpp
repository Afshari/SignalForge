#include "CLParser.h"
#include <iostream>
#include <stdexcept>

namespace SignalForge {

    CLParser CLParser::Parse(int argc, char* argv[])
    {
        CLParser parser;
        parser.m_profileMode = false;
        parser.m_fftMode = false;

        parser.m_configDir = std::filesystem::weakly_canonical(
            std::filesystem::path(argv[0]).parent_path()
        );

        for (int i = 1; i < argc; i++)
        {
            std::string arg = argv[i];
            if (arg == "--profile")
                parser.m_profileMode = true;
            else if (arg == "--fft")
                parser.m_fftMode = true;
            else if (arg == "--pipeline")
                parser.m_pipelineMode = true;
            else if (arg == "--pipeline-sha256")
                parser.m_pipelineSha256Mode = true;
            else if (arg == "--grpc")
                parser.m_grpcMode = true;
            else if (arg == "--config" && i + 1 < argc)
                parser.m_configDir = std::filesystem::weakly_canonical(
                    std::filesystem::path(argv[++i])
                );
            else
                std::cerr << "[WARN] Unknown argument: " << arg << std::endl;
        }
        return parser;
    }

    Config CLParser::LoadConfig(const std::filesystem::path& configDir)
    {
        auto configPath = configDir / "config.json";
        if (!std::filesystem::exists(configPath))
        {
            std::cout << "[WARN] config.json not found, using defaults." << std::endl;
            return Config::Default();
        }
        auto config = Config::Load(configPath);

        if (config.input_dir.is_relative())
            config.input_dir = std::filesystem::weakly_canonical(
                configDir / config.input_dir);
        if (config.output_dir.is_relative())
            config.output_dir = std::filesystem::weakly_canonical(
                configDir / config.output_dir);
        if (config.test_data_dir.is_relative())
            config.test_data_dir = std::filesystem::weakly_canonical(
                configDir / config.test_data_dir);

        return config;
    }

} // namespace SignalForge