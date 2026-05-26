#pragma once
#include <string>
#include <chrono>
#include <iostream>
#include <sstream>

namespace SignalForge {

	class Utils
	{
	public:
		inline static std::string NowISO8601();
		inline static std::string HashToHex(const uint64_t* h_hash);
	};

	// --------------------------------------------------------------------------------
	std::string Utils::HashToHex(const uint64_t* h_hash)
	{
		std::ostringstream oss;
		for (int i = 0; i < 4; i++)
			oss << std::hex << std::setw(16) << std::setfill('0') << h_hash[i];
		return oss.str();
	}

	// --------------------------------------------------------------------------------
	std::string Utils::NowISO8601()
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


} // namespace SignalForge


