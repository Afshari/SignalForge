#include "RedisClient.h"
#include <iostream>
#include <cstring>

namespace SignalForge {

    // --------------------------------------------------------------------------------
    RedisClient::RedisClient(const std::string& host, int port, int db)
    {
        m_ctx = nullptr;
        m_db = db;

#ifdef _WIN32
        char* env_host = nullptr;
        char* env_port = nullptr;
        size_t len = 0;
        _dupenv_s(&env_host, &len, "REDIS_HOST");
        _dupenv_s(&env_port, &len, "REDIS_PORT");
#else
        const char* env_host = std::getenv("REDIS_HOST");
        const char* env_port = std::getenv("REDIS_PORT");
#endif

        m_host = env_host ? env_host : host;
        m_port = env_port ? std::stoi(env_port) : port;

#ifdef _WIN32
        free(env_host);
        free(env_port);
#endif
    }

    // --------------------------------------------------------------------------------
    RedisClient::~RedisClient()
    {
        Disconnect();
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::Connect()
    {
        m_ctx = redisConnect(m_host.c_str(), m_port);
        if (!m_ctx || m_ctx->err)
        {
            if (m_ctx)
            {
                std::cerr << "[Redis] Connection error: " << m_ctx->errstr << std::endl;
                redisFree(m_ctx);
                m_ctx = nullptr;
            }
            else
                std::cerr << "[Redis] Cannot allocate context" << std::endl;
            return false;
        }

        // Connection succeeded - select the correct database
        redisReply* reply = (redisReply*)redisCommand(m_ctx, "SELECT %d", m_db);
        if (reply) freeReplyObject(reply);
        return true;
    }

    // --------------------------------------------------------------------------------
    void RedisClient::Disconnect()
    {
        if (m_ctx)
        {
            redisFree(m_ctx);
            m_ctx = nullptr;
        }
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::IsConnected() const
    {
        return m_ctx != nullptr && m_ctx->err == 0;
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::Ping()
    {
        if (!IsConnected()) return false;
        redisReply* reply = (redisReply*)redisCommand(m_ctx, "PING");
        bool ok = reply && reply->type == REDIS_REPLY_STATUS &&
            std::string(reply->str) == "PONG";
        freeReplyObject(reply);
        return ok;
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::CheckReply(redisReply* reply)
    {
        if (!reply)
        {
            std::cerr << "[Redis] Null reply" << std::endl;
            return false;
        }
        if (reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << "[Redis] Error: " << reply->str << std::endl;
            freeReplyObject(reply);
            return false;
        }
        freeReplyObject(reply);
        return true;
    }

    // --------------------------------------------------------------------------------
    // key: sha256 hex string
    // value: ISO 8601 timestamp - when the file content was first seen
    bool RedisClient::SetHash(const std::string& hash, const std::string& timestamp)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "sha256:" + hash;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "SET %s %s", redisKey.c_str(), timestamp.c_str());
        return CheckReply(reply);
    }

    // --------------------------------------------------------------------------------
    std::optional<std::string> RedisClient::GetHash(const std::string& hash)
    {
        if (!IsConnected()) return std::nullopt;
        std::string redisKey = "sha256:" + hash;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "GET %s", redisKey.c_str());
        if (!reply || reply->type == REDIS_REPLY_NIL)
        {
            freeReplyObject(reply);
            return std::nullopt;
        }
        std::string result(reply->str);
        freeReplyObject(reply);
        return result;
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::HashExists(const std::string& hash)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "sha256:" + hash;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "EXISTS %s", redisKey.c_str());
        if (!reply) return false;
        bool exists = reply->integer == 1;
        freeReplyObject(reply);
        return exists;
    }

    // --------------------------------------------------------------------------------
    // key: sha256 hex string (same hash used in SetHash for the same file)
    // data: fft_size/2+1 floats
    bool RedisClient::SetMagnitudes(const std::string& hash,
        const float* data, uint32_t size)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "fft:" + hash;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "SET %s %b", redisKey.c_str(),
            (const char*)data, (size_t)(size * sizeof(float)));
        return CheckReply(reply);
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::GetMagnitudes(const std::string& hash, std::vector<float>& out)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "fft:" + hash;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "GET %s", redisKey.c_str());
        if (!reply || reply->type == REDIS_REPLY_NIL)
        {
            freeReplyObject(reply);
            return false;
        }
        uint32_t count = static_cast<uint32_t>(reply->len / sizeof(float));
        out.assign((float*)reply->str, (float*)reply->str + count);
        freeReplyObject(reply);
        return true;
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::MagnitudesExist(const std::string& hash)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "fft:" + hash;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "EXISTS %s", redisKey.c_str());
        if (!reply) return false;
        bool exists = reply->integer == 1;
        freeReplyObject(reply);
        return exists;
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::SetFftMag(const std::string& xxhash_hex,
        const float* data, uint32_t size)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "fft_mag:0:" + xxhash_hex;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "SET %s %b", redisKey.c_str(),
            (const char*)data, (size_t)(size * sizeof(float)));
        return CheckReply(reply);
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::GetFftMag(const std::string& xxhash_hex, std::vector<float>& out)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "fft_mag:0:" + xxhash_hex;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "GET %s", redisKey.c_str());
        if (!reply || reply->type == REDIS_REPLY_NIL)
        {
            freeReplyObject(reply);
            return false;
        }
        uint32_t count = static_cast<uint32_t>(reply->len / sizeof(float));
        out.assign((float*)reply->str, (float*)reply->str + count);
        freeReplyObject(reply);
        return true;
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::FftMagExists(const std::string& xxhash_hex)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "fft_mag:0:" + xxhash_hex;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "EXISTS %s", redisKey.c_str());
        if (!reply) return false;
        bool exists = reply->integer == 1;
        freeReplyObject(reply);
        return exists;
    }

    // --------------------------------------------------------------------------------
    void RedisClient::FlushAll()
    {
        if (!IsConnected()) return;
        redisReply* reply = (redisReply*)redisCommand(m_ctx, "FLUSHALL");
        freeReplyObject(reply);
    }

} // namespace SignalForge