#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "BlockingQueue.h"

namespace coordination::thread
{
    // 固定大小线程池，全部基于 C++ 标准库线程设施。
    // 队列策略：submit() 在队列满时阻塞等待空位；try_submit() 在队列满或线程池已关闭时
    // 返回 std::nullopt，不阻塞。max_queue 为 0 时队列无界。
    // 异常策略：任务抛出的异常由 packaged_task 存入 future；worker 另有兜底捕获，
    // 单个任务失败不会终止工作线程。
    // 关闭策略：shutdown(true) 先执行完已排队任务再 join；shutdown(false) 丢弃尚未开始
    // 的任务（已开始的任务会执行完）再 join，被丢弃任务的 future 以 broken_promise 结束。
    // 析构函数等价于 shutdown(true)。
    // 限制：不得在 worker 任务内部调用 shutdown() 或销毁线程池。
    class ThreadPool
    {
    public:
        explicit ThreadPool(std::size_t worker_count, std::size_t max_queue = 0)
            : queue_(max_queue), worker_count_(worker_count)
        {
            if (worker_count_ == 0)
            {
                throw std::invalid_argument("ThreadPool 的 worker_count 不能为 0");
            }

            workers_.reserve(worker_count_);
            try
            {
                for (std::size_t index = 0; index < worker_count_; ++index)
                {
                    workers_.emplace_back(&ThreadPool::worker_loop, this);
                }
            }
            catch (...)
            {
                // 线程创建中途失败：让已创建的 worker 退出并回收，避免 joinable 线程析构时 terminate。
                queue_.close();
                for (std::thread& worker : workers_)
                {
                    if (worker.joinable())
                    {
                        worker.join();
                    }
                }
                throw;
            }
        }

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        ~ThreadPool()
        {
            try
            {
                shutdown(true);
            }
            catch (...)
            {
                // 析构函数不传播异常；正常路径下 shutdown 不会失败。
            }
        }

        // 提交任务并返回 future；队列满时阻塞。线程池已关闭时抛出 std::runtime_error。
        template <typename F, typename... Args>
        auto submit(F&& callable, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
        {
            using result_type = std::invoke_result_t<F, Args...>;

            if (!running_.load(std::memory_order_acquire))
            {
                throw std::runtime_error("ThreadPool 已关闭，拒绝提交任务");
            }

            std::shared_ptr<std::packaged_task<result_type()>> task =
                make_task(std::forward<F>(callable), std::forward<Args>(args)...);
            std::future<result_type> future = task->get_future();
            if (!queue_.push([task]
            {
                (*task)();
            }))
            {
                throw std::runtime_error("ThreadPool 已关闭，拒绝提交任务");
            }
            return future;
        }

        // 尝试提交任务：队列满或线程池已关闭时返回 std::nullopt，不阻塞。
        template <typename F, typename... Args>
        auto try_submit(F&& callable, Args&&... args)
            -> std::optional<std::future<std::invoke_result_t<F, Args...>>>
        {
            using result_type = std::invoke_result_t<F, Args...>;

            if (!running_.load(std::memory_order_acquire))
            {
                return std::nullopt;
            }

            std::shared_ptr<std::packaged_task<result_type()>> task =
                make_task(std::forward<F>(callable), std::forward<Args>(args)...);
            std::future<result_type> future = task->get_future();
            if (!queue_.try_push([task]
            {
                (*task)();
            }))
            {
                return std::nullopt;
            }
            return std::optional<std::future<result_type>>(std::move(future));
        }

        // 关闭线程池并 join 所有 worker。多次调用安全：首次调用决定 wait 语义，
        // 其余调用等待关闭完成。不得在 worker 线程内部调用。
        void shutdown(bool wait = true)
        {
            if (is_worker_thread())
            {
                throw std::logic_error("ThreadPool::shutdown 不能在 worker 线程内部调用");
            }

            {
                std::unique_lock<std::mutex> lock(lifecycle_mutex_);
                if (shutdown_started_)
                {
                    stopped_cv_.wait(lock, [this]
                    {
                        return shutdown_finished_;
                    });
                    return;
                }

                shutdown_started_ = true;
                running_.store(false, std::memory_order_release);
            }

            // 先关闭队列唤醒阻塞中的 submit/pop；wait=false 时再丢弃尚未开始的任务。
            queue_.close();
            if (!wait)
            {
                queue_.clear();
            }

            for (std::thread& worker : workers_)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }

            {
                std::lock_guard<std::mutex> lock(lifecycle_mutex_);
                shutdown_finished_ = true;
            }
            stopped_cv_.notify_all();
        }

        std::size_t worker_count() const noexcept
        {
            return worker_count_;
        }

        // 已入队但尚未开始执行的任务数。
        std::size_t pending() const
        {
            return queue_.size();
        }

        bool running() const noexcept
        {
            return running_.load(std::memory_order_acquire);
        }

    private:
        using task_type = std::function<void()>;

        // 把可调用对象和实参 decay-copy 进 packaged_task；实参以右值传入任务，
        // 因此支持 move-only 参数，需要引用语义时请使用 std::ref。
        template <typename F, typename... Args>
        static auto make_task(F&& callable, Args&&... args)
            -> std::shared_ptr<std::packaged_task<std::invoke_result_t<F, Args...>()>>
        {
            using result_type = std::invoke_result_t<F, Args...>;
            using stored_type = std::tuple<std::decay_t<F>, std::decay_t<Args>...>;

            return std::make_shared<std::packaged_task<result_type()>>(
                [stored = stored_type(std::forward<F>(callable), std::forward<Args>(args)...)]() mutable
                    -> result_type
                {
                    return std::apply(
                        [](auto&& function, auto&&... values) -> result_type
                        {
                            return std::invoke(
                                std::forward<decltype(function)>(function),
                                std::forward<decltype(values)>(values)...);
                        },
                        std::move(stored));
                });
        }

        bool is_worker_thread() const noexcept
        {
            return current_pool_ == this;
        }

        void worker_loop() noexcept
        {
            ThreadPool* const previous_pool = current_pool_;
            current_pool_ = this;

            task_type task;
            while (queue_.pop(task))
            {
                try
                {
                    task();
                }
                catch (...)
                {
                    // packaged_task 已把任务异常存入 future；这里兜底，绝不让异常逃出线程。
                }
            }

            current_pool_ = previous_pool;
        }

        BlockingQueue<task_type> queue_;
        std::size_t worker_count_;
        std::vector<std::thread> workers_;
        std::atomic<bool> running_{true};
        std::mutex lifecycle_mutex_;
        std::condition_variable stopped_cv_;
        bool shutdown_started_ = false;
        bool shutdown_finished_ = false;
        inline static thread_local ThreadPool* current_pool_ = nullptr;
    };
}
