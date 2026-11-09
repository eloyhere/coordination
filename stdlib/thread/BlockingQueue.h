#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace coordination::thread
{
    // 有界/无界阻塞队列：capacity 为 0 表示无界。
    // close() 只拒绝后续入队并唤醒所有等待者；已入队元素仍可取出（排空语义），
    // pop/pop_for 在队列为空且已关闭时返回 false。
    template <typename T>
    class BlockingQueue
    {
    public:
        using value_type = T;
        using size_type = std::size_t;

        explicit BlockingQueue(size_type capacity = 0)
            : capacity_(capacity), closed_(false)
        {
        }

        BlockingQueue(const BlockingQueue&) = delete;
        BlockingQueue& operator=(const BlockingQueue&) = delete;

        // 阻塞入队：队列满时等待空位；队列已关闭时返回 false。
        bool push(T value)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_full_.wait(lock, [this]
            {
                return closed_ || capacity_ == 0 || queue_.size() < capacity_;
            });
            if (closed_)
            {
                return false;
            }

            queue_.push_back(std::move(value));
            not_empty_.notify_one();
            return true;
        }

        // 非阻塞入队：队列满或已关闭时立即返回 false。
        bool try_push(T value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || (capacity_ != 0 && queue_.size() >= capacity_))
            {
                return false;
            }

            queue_.push_back(std::move(value));
            not_empty_.notify_one();
            return true;
        }

        // 阻塞出队：队列为空时等待；已关闭且为空时返回 false。
        bool pop(T& value)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_empty_.wait(lock, [this]
            {
                return closed_ || !queue_.empty();
            });
            if (queue_.empty())
            {
                return false;
            }

            value = std::move(queue_.front());
            queue_.pop_front();
            not_full_.notify_one();
            return true;
        }

        // 限时出队：超时或已关闭且为空时返回 false。
        template <typename Rep, typename Period>
        bool pop_for(T& value, const std::chrono::duration<Rep, Period>& timeout)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const bool ready = not_empty_.wait_for(lock, timeout, [this]
            {
                return closed_ || !queue_.empty();
            });
            if (!ready || queue_.empty())
            {
                return false;
            }

            value = std::move(queue_.front());
            queue_.pop_front();
            not_full_.notify_one();
            return true;
        }

        // 关闭队列：拒绝后续入队，并唤醒所有等待者；剩余元素仍可取出。
        void close()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                closed_ = true;
            }
            not_empty_.notify_all();
            not_full_.notify_all();
        }

        // 清空尚未取出的元素；通常与 close() 配合用于丢弃排队任务。
        void clear()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.clear();
            not_full_.notify_all();
        }

        bool closed() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return closed_;
        }

        bool empty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.empty();
        }

        size_type size() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.size();
        }

        size_type capacity() const noexcept
        {
            return capacity_;
        }

        bool bounded() const noexcept
        {
            return capacity_ != 0;
        }

    private:
        const size_type capacity_;
        mutable std::mutex mutex_;
        std::condition_variable not_empty_;
        std::condition_variable not_full_;
        std::deque<T> queue_;
        bool closed_;
    };
}
