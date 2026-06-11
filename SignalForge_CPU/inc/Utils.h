#pragma once
#include <string>
#include <chrono>
#include <sstream>
#include <vector>
#include <filesystem>
#include <iomanip>

namespace SignalForge {

	class Utils
	{
	public:
		inline static std::string NowISO8601();
		inline static std::string HashToHex(const uint64_t* h_hash);
		inline static std::vector<std::filesystem::path> ScanWavFiles(const std::filesystem::path& dir);
	};

	// --------------------------------------------------------------------------------
	inline std::string Utils::HashToHex(const uint64_t* h_hash)
	{
		std::ostringstream oss;
		for (int i = 0; i < 4; i++)
			oss << std::hex << std::setw(16) << std::setfill('0') << h_hash[i];
		return oss.str();
	}

	// --------------------------------------------------------------------------------
	inline std::string Utils::NowISO8601()
	{
		auto now = std::chrono::system_clock::now();
		std::time_t t = std::chrono::system_clock::to_time_t(now);
		std::tm tm{};
#ifdef _WIN32
		gmtime_s(&tm, &t);
#else
		gmtime_r(&t, &tm);
#endif
		std::ostringstream oss;
		oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
		return oss.str();
	}

	// --------------------------------------------------------------------------------
	inline std::vector<std::filesystem::path> Utils::ScanWavFiles(
		const std::filesystem::path& dir)
	{
		std::vector<std::filesystem::path> paths;
		if (!std::filesystem::exists(dir))
			return paths;

		for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
		{
			if (!entry.is_regular_file()) continue;
			if (entry.path().extension() != ".wav") continue;

			// accept files in root dir or exactly one level deep
			auto parent = entry.path().parent_path();
			if (parent != dir && parent.parent_path() != dir) continue;

			paths.push_back(entry.path());
		}
		return paths;
	}

} // namespace SignalForge


