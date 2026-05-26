#pragma once
#include <hiredis/hiredis.h>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace SignalForge {

    class RedisClient
    {
    public:
        RedisClient(const std::string& host = "127.0.0.1", int port = 6379, int db = 0);
        ~RedisClient();

        // Connection
        bool Connect();
        void Disconnect();
        bool IsConnected() const;

        // SHA-256 hash operations
        // key: sha256 hex string
        // value: ISO 8601 timestamp of when the file was first seen
        bool SetHash(const std::string& hash, const std::string& timestamp);
        std::optional<std::string> GetHash(const std::string& hash);
        bool HashExists(const std::string& hash);

        // FFT magnitude operations
        // key: sha256 hex string (same hash used as key for SetHash)
        // value: raw float array (fft_size/2+1 floats)
        bool SetMagnitudes(const std::string& hash, const float* data, uint32_t size);
        bool GetMagnitudes(const std::string& hash, std::vector<float>& out);
        bool MagnitudesExist(const std::string& hash);

        // Utility
        bool Ping();
        void FlushAll();

    private:
        std::string   m_host;
        int           m_port;
        int           m_db;
        redisContext* m_ctx = nullptr;

        bool CheckReply(redisReply* reply);
    };

} // namespace SignalForge