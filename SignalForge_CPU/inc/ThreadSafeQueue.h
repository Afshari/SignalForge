#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

template<typename T>
class ThreadSafeQueue
{
public:
    // Push an item into the queue
    void push(T item)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(std::move(item));
        }
        m_cv.notify_one();
    }

    // Pop an item  
    // blocks until item available or queue is closed
    // Returns std::nullopt if queue is closed and empty
    std::optional<T> pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty() || m_closed; });

        if (m_queue.empty())
            return std::nullopt;

        T item = std::move(m_queue.front());
        m_queue.pop();
        return item;
    }

    // Close the queue 
    // unblocks all waiting threads
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_cv.notify_all();
    }

    // Check if queue is closed and empty
    bool done() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed && m_queue.empty();
    }

private:
    std::queue<T>           m_queue;
    mutable std::mutex      m_mutex;
    std::condition_variable m_cv;
    bool                    m_closed = false;
};