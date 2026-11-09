#include "../stdlib/thread/BlockingQueue.h"
#include "../stdlib/thread/ThreadPool.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using coordination::thread::BlockingQueue;
using coordination::thread::ThreadPool;

// 失败即打印原因并返回 false；main 汇总后只输出一行结果。
#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cout << "FAIL: " << #condition << " (line " << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (false)

namespace
{
    bool test_concurrent_sum()
    {
        ThreadPool pool(8);
        CHECK(pool.worker_count() == std::size_t{8});

        std::atomic<long long> total{0};
        std::vector<std::future<void>> futures;
        futures.reserve(1000);
        for (int i = 0; i < 1000; ++i)
        {
            futures.push_back(pool.submit([&total, i]
            {
                total.fetch_add(i, std::memory_order_relaxed);
            }));
        }

        for (std::future<void>& future : futures)
        {
            future.get();
        }
        CHECK(total.load(std::memory_order_relaxed) == 999LL * 1000LL / 2LL);
        CHECK(pool.pending() == std::size_t{0});
        return true;
    }

    bool test_future_and_exception()
    {
        ThreadPool pool(4);

        std::future<int> answer = pool.submit([](int left, int right)
        {
            return left + right;
        }, 20, 22);
        CHECK(answer.get() == 42);

        // move-only 实参应可随任务转移。
        std::future<int> moved = pool.submit([](std::unique_ptr<int> value)
        {
            return *value;
        }, std::make_unique<int>(7));
        CHECK(moved.get() == 7);

        // 任务异常写入 future，由 get() 重新抛出。
        std::future<int> failure = pool.submit([]() -> int
        {
            throw std::runtime_error("boom");
        });
        bool caught = false;
        try
        {
            (void)failure.get();
        }
        catch (const std::runtime_error& error)
        {
            caught = std::string(error.what()) == "boom";
        }
        CHECK(caught);
        CHECK(pool.running());

        // 异常任务之后 worker 仍可继续工作。
        std::vector<std::future<int>> futures;
        futures.reserve(32);
        for (int i = 0; i < 32; ++i)
        {
            futures.push_back(pool.submit([i]
            {
                return i * i;
            }));
        }
        for (int i = 0; i < 32; ++i)
        {
            CHECK(futures[static_cast<std::size_t>(i)].get() == i * i);
        }
        return true;
    }

    bool test_shutdown_wait_true()
    {
        ThreadPool pool(4);
        std::atomic<int> completed{0};
        std::vector<std::future<void>> futures;
        futures.reserve(100);
        for (int i = 0; i < 100; ++i)
        {
            futures.push_back(pool.submit([&completed]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                completed.fetch_add(1, std::memory_order_relaxed);
            }));
        }

        pool.shutdown(true);
        CHECK(!pool.running());
        CHECK(pool.pending() == std::size_t{0});
        CHECK(completed.load(std::memory_order_relaxed) == 100);
        for (std::future<void>& future : futures)
        {
            future.get();
        }
        return true;
    }

    bool test_shutdown_discard()
    {
        // 单个 worker 被 gate 卡住，队列塞满后 shutdown(false) 应丢弃排队任务。
        ThreadPool pool(1, 4);
        std::promise<void> started;
        std::promise<void> release;
        std::shared_future<void> gate = release.get_future().share();
        std::future<void> started_future = started.get_future();
        std::future<void> blocker = pool.submit([&started, gate]
        {
            started.set_value();
            gate.wait();
        });
        started_future.wait();

        std::atomic<int> executed{0};
        std::vector<std::future<void>> queued;
        queued.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            queued.push_back(pool.submit([&executed]
            {
                executed.fetch_add(1, std::memory_order_relaxed);
            }));
        }
        CHECK(pool.pending() == std::size_t{4});

        std::thread stopper([&pool]
        {
            pool.shutdown(false);
        });

        bool discarded = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (pool.pending() == std::size_t{0})
            {
                discarded = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        release.set_value();
        stopper.join();
        blocker.get();

        CHECK(discarded);
        CHECK(!pool.running());
        CHECK(pool.pending() == std::size_t{0});
        CHECK(executed.load(std::memory_order_relaxed) == 0);

        // 被丢弃任务的 future 以 broken_promise 结束，不会永久挂起。
        bool broken_promise = false;
        try
        {
            queued.front().get();
        }
        catch (const std::future_error&)
        {
            broken_promise = true;
        }
        CHECK(broken_promise);

        // 重复 shutdown 安全，首次调用的语义不会再变化。
        pool.shutdown(true);
        CHECK(!pool.running());

        // 关闭后 submit 抛异常，try_submit 返回空 optional。
        bool submit_threw = false;
        try
        {
            (void)pool.submit([] { return 1; });
        }
        catch (const std::runtime_error&)
        {
            submit_threw = true;
        }
        CHECK(submit_threw);
        CHECK(!pool.try_submit([] { return 2; }).has_value());
        return true;
    }

    bool test_bounded_submit()
    {
        // 1 个 worker、队列容量 2：try_submit 在满时失败，submit 阻塞到有空位。
        ThreadPool pool(1, 2);
        std::promise<void> started;
        std::promise<void> release;
        std::shared_future<void> gate = release.get_future().share();
        std::future<void> started_future = started.get_future();
        std::future<void> blocker = pool.submit([&started, gate]
        {
            started.set_value();
            gate.wait();
        });
        started_future.wait();

        std::future<int> first = pool.submit([] { return 1; });
        std::future<int> second = pool.submit([] { return 2; });
        CHECK(!pool.try_submit([] { return 3; }).has_value());
        CHECK(pool.pending() == std::size_t{2});

        std::promise<void> submit_returned;
        std::future<void> submit_returned_future = submit_returned.get_future();
        std::future<int> third;
        std::thread submitter([&pool, &third, &submit_returned]
        {
            third = pool.submit([] { return 4; });
            submit_returned.set_value();
        });

        const std::future_status blocked_status =
            submit_returned_future.wait_for(std::chrono::milliseconds(100));

        release.set_value();
        submitter.join();

        CHECK(blocked_status == std::future_status::timeout);
        CHECK(third.get() == 4);
        CHECK(first.get() == 1);
        CHECK(second.get() == 2);
        blocker.get();
        return true;
    }

    bool test_blocking_queue()
    {
        BlockingQueue<int> queue;
        CHECK(!queue.bounded());
        CHECK(queue.capacity() == std::size_t{0});
        CHECK(queue.empty());
        CHECK(queue.size() == std::size_t{0});

        // pop_for 超时返回 false，并且确实等待了接近超时时长。
        int value = 0;
        const auto begin = std::chrono::steady_clock::now();
        CHECK(!queue.pop_for(value, std::chrono::milliseconds(30)));
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - begin);
        CHECK(elapsed.count() >= 20);

        CHECK(queue.push(7));
        CHECK(!queue.empty());
        CHECK(queue.size() == std::size_t{1});
        CHECK(queue.pop_for(value, std::chrono::milliseconds(0)));
        CHECK(value == 7);

        CHECK(queue.try_push(8));
        CHECK(queue.try_push(9));
        CHECK(queue.pop(value));
        CHECK(value == 8);
        CHECK(queue.pop(value));
        CHECK(value == 9);

        // close 唤醒阻塞中的 pop，并拒绝后续入队。
        std::atomic<bool> woken{false};
        std::thread consumer([&queue, &woken]
        {
            int item = 0;
            if (!queue.pop(item))
            {
                woken.store(true, std::memory_order_relaxed);
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        queue.close();
        consumer.join();
        CHECK(woken.load(std::memory_order_relaxed));

        CHECK(queue.closed());
        CHECK(!queue.push(10));
        CHECK(!queue.try_push(11));
        CHECK(!queue.pop(value));
        int untouched = 123;
        CHECK(!queue.pop_for(untouched, std::chrono::milliseconds(1)));
        CHECK(untouched == 123);
        CHECK(queue.empty());
        return true;
    }

    bool test_bounded_queue()
    {
        BlockingQueue<int> queue(2);
        CHECK(queue.bounded());
        CHECK(queue.capacity() == std::size_t{2});
        CHECK(queue.push(1));
        CHECK(queue.push(2));
        CHECK(!queue.try_push(3));

        int value = 0;
        CHECK(queue.pop(value));
        CHECK(value == 1);
        CHECK(queue.try_push(4));
        CHECK(queue.pop(value));
        CHECK(value == 2);
        CHECK(queue.pop(value));
        CHECK(value == 4);

        // 关闭后仍可排空已入队元素。
        CHECK(queue.push(5));
        CHECK(queue.push(6));
        queue.close();
        CHECK(queue.pop(value));
        CHECK(value == 5);
        CHECK(queue.pop(value));
        CHECK(value == 6);
        CHECK(!queue.pop(value));
        return true;
    }

    bool test_order_independence()
    {
        ThreadPool pool(4);
        constexpr int count = 128;
        std::vector<int> values(static_cast<std::size_t>(count), 0);
        std::vector<std::future<void>> futures;
        futures.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            futures.push_back(pool.submit([&values, i]
            {
                // 完成顺序被打乱，但每个任务只写自己的槽位。
                std::this_thread::sleep_for(std::chrono::microseconds((i * 37) % 11 * 100));
                values[static_cast<std::size_t>(i)] = i * i;
            }));
        }
        for (std::future<void>& future : futures)
        {
            future.get();
        }
        for (int i = 0; i < count; ++i)
        {
            CHECK(values[static_cast<std::size_t>(i)] == i * i);
        }

        // 同一原子计数由多任务累加，最终值只取决于任务集合，与完成顺序无关。
        std::atomic<int> completed{0};
        std::vector<std::future<void>> sum_futures;
        sum_futures.reserve(100);
        for (int i = 0; i < 100; ++i)
        {
            sum_futures.push_back(pool.submit([&completed]
            {
                completed.fetch_add(1, std::memory_order_relaxed);
            }));
        }
        for (std::future<void>& future : sum_futures)
        {
            future.get();
        }
        CHECK(completed.load(std::memory_order_relaxed) == 100);
        return true;
    }

    bool test_worker_count_and_pending()
    {
        bool invalid_thrown = false;
        try
        {
            ThreadPool invalid(0);
            (void)invalid;
        }
        catch (const std::invalid_argument&)
        {
            invalid_thrown = true;
        }
        CHECK(invalid_thrown);

        ThreadPool pool(1);
        CHECK(pool.worker_count() == std::size_t{1});
        CHECK(pool.pending() == std::size_t{0});
        CHECK(pool.running());

        std::promise<void> started;
        std::promise<void> release;
        std::shared_future<void> gate = release.get_future().share();
        std::future<void> started_future = started.get_future();
        std::future<void> blocker = pool.submit([&started, gate]
        {
            started.set_value();
            gate.wait();
        });
        started_future.wait();
        CHECK(pool.pending() == std::size_t{0});

        std::future<int> one = pool.submit([] { return 1; });
        std::future<int> two = pool.submit([] { return 2; });
        std::future<int> three = pool.submit([] { return 3; });
        CHECK(pool.pending() == std::size_t{3});

        release.set_value();
        CHECK(one.get() == 1);
        CHECK(two.get() == 2);
        CHECK(three.get() == 3);
        blocker.get();

        // try_submit 成功路径返回可用的 future。
        std::optional<std::future<int>> optional_future = pool.try_submit([] { return 5; });
        CHECK(optional_future.has_value());
        CHECK(optional_future->get() == 5);
        CHECK(pool.pending() == std::size_t{0});
        CHECK(pool.running());
        return true;
    }

    bool test_destructor_drains()
    {
        std::atomic<int> completed{0};
        {
            ThreadPool pool(4);
            for (int i = 0; i < 200; ++i)
            {
                (void)pool.submit([&completed]
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    completed.fetch_add(1, std::memory_order_relaxed);
                });
            }
        }
        CHECK(completed.load(std::memory_order_relaxed) == 200);
        return true;
    }

    bool test_stress()
    {
        ThreadPool pool(8);
        constexpr int count = 10000;
        std::atomic<int> completed{0};
        std::vector<std::future<int>> futures;
        futures.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            futures.push_back(pool.submit([&completed, i]
            {
                completed.fetch_add(1, std::memory_order_relaxed);
                return i;
            }));
        }

        long long total = 0;
        for (std::future<int>& future : futures)
        {
            total += future.get();
        }
        CHECK(completed.load(std::memory_order_relaxed) == count);
        CHECK(total == 9999LL * 10000LL / 2LL);
        CHECK(pool.pending() == std::size_t{0});
        return true;
    }
}

int main()
{
    const bool passed =
        test_concurrent_sum()
        && test_future_and_exception()
        && test_shutdown_wait_true()
        && test_shutdown_discard()
        && test_bounded_submit()
        && test_blocking_queue()
        && test_bounded_queue()
        && test_order_independence()
        && test_worker_count_and_pending()
        && test_destructor_drains()
        && test_stress();

    if (!passed)
    {
        return 1;
    }

    std::cout << "PASS: thread_pool" << std::endl;
    return 0;
}
