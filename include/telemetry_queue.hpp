#pragma once

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace telemetry
{

enum class OverflowPolicy
{
    DropNewest,
    DropOldest,
    Block
};

template <typename T>
class BoundedQueue
{
private:
    mutable std::mutex mtx;
    std::condition_variable not_empty;
    std::condition_variable not_full;
    std::deque<T> items;
    std::size_t max_size;
    bool closed{false};

public:
    explicit BoundedQueue(std::size_t capacity)
        : max_size(capacity == 0 ? 1 : capacity)
    {
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    bool try_push(T value, OverflowPolicy policy, std::size_t* dropped = nullptr)
    {
        std::unique_lock<std::mutex> lock(mtx);

        if(closed)
            return false;

        if(items.size() >= max_size)
        {
            if(policy == OverflowPolicy::DropNewest)
            {
                if(dropped)
                    ++(*dropped);
                return false;
            }

            if(policy == OverflowPolicy::DropOldest)
            {
                items.pop_front();
                if(dropped)
                    ++(*dropped);
            }
            else
            {
                not_full.wait(lock, [this] { return closed || items.size() < max_size; });
                if(closed)
                    return false;
            }
        }

        items.emplace_back(std::move(value));
        not_empty.notify_one();
        return true;
    }

    bool pop(T& value)
    {
        std::unique_lock<std::mutex> lock(mtx);
        not_empty.wait(lock, [this] { return closed || !items.empty(); });

        if(items.empty())
            return false;

        value = std::move(items.front());
        items.pop_front();
        not_full.notify_one();
        return true;
    }

    template <typename Rep, typename Period>
    bool pop_for(T& value, const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mtx);
        if(!not_empty.wait_for(lock, timeout, [this] { return closed || !items.empty(); }))
            return false;

        if(items.empty())
            return false;

        value = std::move(items.front());
        items.pop_front();
        not_full.notify_one();
        return true;
    }

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            closed = true;
        }

        not_empty.notify_all();
        not_full.notify_all();
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mtx);
        return items.size();
    }

    std::size_t capacity() const
    {
        return max_size;
    }
};

} // namespace telemetry
