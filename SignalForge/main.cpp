#include "CLParser.h"
#include "AppRunner.h"
#include "Config.h"
#include "SignalForgePipeline.h"
#include "Utils.h"
#include <iostream>
#include <atomic>
#include <csignal>

#pragma warning(push)
#pragma warning(disable: 4996)
#include <grpcpp/grpcpp.h>
#pragma warning(pop)

static std::atomic<bool> g_shutdown = false;
static SignalForge::SignalForgePipeline* g_pipeline = nullptr;

static void SignalHandler(int /*signal*/)
{
	g_shutdown = true;
	if (g_pipeline)
		g_pipeline->Stop();
}

int main(int argc, char* argv[])
{
	SignalForge::CLParser args = SignalForge::CLParser::Parse(argc, argv);

	std::string mode = args.IsProfileMode() ? "PROFILE"
		: args.IsFftMode() ? "FFT"
		: args.IsPipelineMode() ? "PIPELINE"
		: args.IsGrpcMode() ? "GRPC"
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
		if (args.IsGrpcMode())
		{
			std::signal(SIGINT, SignalHandler);
			std::signal(SIGTERM, SignalHandler);

			std::filesystem::create_directories(config.input_dir);
			SignalForge::SignalForgePipeline pipeline({}, config);
			g_pipeline = &pipeline;
			pipeline.Run();
			g_pipeline = nullptr;
			return 0;
		}
		if (args.IsPipelineMode())
		{
			auto files = SignalForge::Utils::ScanWavFiles(config.input_dir);
			std::vector<std::string> filepaths;
			for (const auto& p : files)
				filepaths.push_back(p.string());
			SignalForge::SignalForgePipeline pipeline(filepaths, config);
			pipeline.Run();
			return 0;
		}
		return SignalForge::AppRunner::RunHash(config);
	}
	catch (const std::exception& e)
	{
		std::cerr << "[ERROR] Pipeline failed: " << e.what() << std::endl;
		return 1;
	}
}