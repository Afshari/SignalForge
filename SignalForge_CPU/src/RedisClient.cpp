#include "RedisClient.h"
#include <iostream>
#include <cstring>

namespace SignalForge {

    // --------------------------------------------------------------------------------
    RedisClient::RedisClient(const std::string& host, int port)
        : m_host(host), m_port(port), m_ctx(nullptr)
    {
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
    bool RedisClient::SetHash(const std::string& key, const std::string& hash)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "sha256:" + key;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "SET %s %s", redisKey.c_str(), hash.c_str());
        return CheckReply(reply);
    }

    // --------------------------------------------------------------------------------
    std::optional<std::string> RedisClient::GetHash(const std::string& key)
    {
        if (!IsConnected()) return std::nullopt;
        std::string redisKey = "sha256:" + key;
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
    bool RedisClient::HashExists(const std::string& key)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "sha256:" + key;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "EXISTS %s", redisKey.c_str());
        if (!reply) return false;
        bool exists = reply->integer == 1;
        freeReplyObject(reply);
        return exists;
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::SetMagnitudes(const std::string& key,
        const float* data, uint32_t size)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "fft:" + key;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "SET %s %b", redisKey.c_str(),
            (const char*)data, (size_t)(size * sizeof(float)));
        return CheckReply(reply);
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::GetMagnitudes(const std::string& key, std::vector<float>& out)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "fft:" + key;
        redisReply* reply = (redisReply*)redisCommand(
            m_ctx, "GET %s", redisKey.c_str());
        if (!reply || reply->type == REDIS_REPLY_NIL)
        {
            freeReplyObject(reply);
            return false;
        }
        uint32_t count = reply->len / sizeof(float);
        out.assign((float*)reply->str, (float*)reply->str + count);
        freeReplyObject(reply);
        return true;
    }

    // --------------------------------------------------------------------------------
    bool RedisClient::MagnitudesExist(const std::string& key)
    {
        if (!IsConnected()) return false;
        std::string redisKey = "fft:" + key;
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