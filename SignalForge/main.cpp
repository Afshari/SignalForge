#include "CLParser.h"
#include "AppRunner.h"
#include "Config.h"
#include <iostream>

int main(int argc, char* argv[])
{
    SignalForge::CLParser args = SignalForge::CLParser::Parse(argc, argv);

    std::string mode = args.IsProfileMode() ? "PROFILE"
        : args.IsFftMode() ? "FFT"
        : "HASH";

    std::cout << "[INFO] Config dir: " << args.GetConfigDir() << std::endl;
    std::cout << "[INFO] Mode: " << mode << std::endl;

    SignalForge::Config config;
    try
    {
        config = SignalForge::CLParser::LoadConfig(args.GetConfigDir());
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return 1;
    }

    try
    {
        if (args.IsProfileMode()) return SignalForge::AppRunner::RunProfile(config);
        if (args.IsFftMode())     return SignalForge::AppRunner::RunFFT(config);
        return SignalForge::AppRunner::RunHash(config);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] Pipeline failed: " << e.what() << std::endl;
        return 1;
    }
}