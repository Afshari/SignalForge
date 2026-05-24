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
        RedisClient(const std::string& host = "127.0.0.1", int port = 6379);
        ~RedisClient();

        // Connection
        bool Connect();
        void Disconnect();
        bool IsConnected() const;

        // SHA-256 hash operations
        // key: filename, value: hex hash string
        bool SetHash(const std::string& key, const std::string& hash);
        std::optional<std::string> GetHash(const std::string& key);
        bool HashExists(const std::string& key);

        // FFT magnitude operations
        // key: filename, value: raw float array
        bool SetMagnitudes(const std::string& key, const float* data, uint32_t size);
        bool GetMagnitudes(const std::string& key, std::vector<float>& out);
        bool MagnitudesExist(const std::string& key);

        // Utility
        bool Ping();
        void FlushAll();

    private:
        std::string  m_host;
        int          m_port;
        redisContext* m_ctx = nullptr;

        bool CheckReply(redisReply* reply);
    };

} // namespace SignalForge