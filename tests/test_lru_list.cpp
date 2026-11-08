#include "../structure/lru/LeastRarelyUsageList.h"

#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <list>
#include <random>
#include <string>
#include <vector>

// 失败立即打印原因并退出；全部通过时只输出一行 PASS。
#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cout << "FAIL: " << #condition << " (line " << __LINE__ << ")" << std::endl; \
            return 1; \
        } \
    } while (false)

using coordination::lru::LeastRarelyUsageList;
using coordination::pool::ObjectPool;

namespace
{
    using int_list = LeastRarelyUsageList<int>;
    using int_handle = int_list::handle_type;
    using string_list = LeastRarelyUsageList<std::string>;
    using string_handle = string_list::handle_type;

    std::vector<int> ints(std::initializer_list<int> values)
    {
        return std::vector<int>(values);
    }

    std::vector<std::string> strings(std::initializer_list<std::string> values)
    {
        return std::vector<std::string>(values);
    }

    std::vector<int> values_of(const std::list<int>& model)
    {
        return std::vector<int>(model.begin(), model.end());
    }

    // 不可默认构造的计数类型：T 只需可拷贝，用存活计数检查池中对象全部析构。
    struct Tracked
    {
        static int alive_count;

        explicit Tracked(int value)
            : value(value)
        {
            ++alive_count;
        }

        Tracked(const Tracked& other)
            : value(other.value)
        {
            ++alive_count;
        }

        Tracked& operator=(const Tracked&) = delete;

        ~Tracked()
        {
            --alive_count;
        }

        int value = 0;
    };

    int Tracked::alive_count = 0;
}

int main()
{
    // 空表：哨兵自环，没有任何元素与句柄。
    {
        LeastRarelyUsageList<int> list;
        CHECK(list.empty());
        CHECK(list.size() == 0);
        CHECK(list.capacity() == 0);
        CHECK(list.pool_alive() == 0);
        CHECK(list.least_recent() == nullptr);
        CHECK(list.most_recent() == nullptr);
        CHECK(list.to_vector().empty());
        CHECK(list.validate());

        const int_handle invalid{};
        CHECK(invalid.index == static_cast<std::size_t>(-1));
        CHECK(!list.touch(invalid));
        CHECK(!list.erase(invalid));
        CHECK(list.value(invalid) == nullptr);
        CHECK(!list.touch(int_handle{0, 0}));
        CHECK(!list.erase(int_handle{0, 0}));
    }

    // ObjectPool 句柄：destroy 后代际递增，同槽位复用后旧句柄必须失效。
    {
        ObjectPool<int> pool;
        int* first = pool.create(7);
        const ObjectPool<int>::SlotHandle handle = pool.make_handle(first);
        CHECK(pool.alive(handle));
        CHECK(pool.resolve(handle) == first);
        CHECK(*pool.resolve(handle) == 7);

        pool.destroy(first);
        CHECK(!pool.alive(handle));
        CHECK(pool.resolve(handle) == nullptr);
        CHECK(pool.make_handle(first).index == static_cast<std::size_t>(-1));

        // 重复 destroy 安全：不会重复析构，也不会重复减少存活计数。
        pool.destroy(first);
        CHECK(pool.alive() == 0);

        int* second = pool.create(8);
        const ObjectPool<int>::SlotHandle reused = pool.make_handle(second);
        CHECK(reused.index == handle.index);
        CHECK(reused.generation != handle.generation);
        CHECK(pool.alive(reused));
        CHECK(!pool.alive(handle));
        CHECK(pool.resolve(handle) == nullptr);
        CHECK(*pool.resolve(reused) == 8);

        CHECK(pool.make_handle(nullptr).index == static_cast<std::size_t>(-1));
        CHECK(!pool.alive(pool.make_handle(nullptr)));

        // clear 后所有旧句柄失效。
        pool.clear();
        CHECK(!pool.alive(reused));
        CHECK(pool.resolve(reused) == nullptr);
        CHECK(pool.alive() == 0);
    }

    // push 压到最近使用端，to_vector 按“最近 → 最久”输出。
    {
        LeastRarelyUsageList<int> list;
        int_handle one = list.push(1);
        int_handle two = list.push(2);
        int_handle three = list.push(3);
        CHECK(one.index != static_cast<std::size_t>(-1));
        CHECK(one.index != two.index);
        CHECK(two.index != three.index);
        CHECK(list.value(one) != nullptr && *list.value(one) == 1);
        CHECK(list.value(two) != nullptr && *list.value(two) == 2);
        CHECK(list.value(three) != nullptr && *list.value(three) == 3);
        CHECK(!list.empty());
        CHECK(list.size() == 3);
        CHECK(list.pool_alive() == 3);
        CHECK(*list.most_recent() == 3);
        CHECK(*list.least_recent() == 1);
        CHECK(list.to_vector() == ints({3, 2, 1}));
        CHECK(list.validate());
    }

    // touch 把命中句柄移到最近使用端；失效或越界句柄返回 false。
    {
        LeastRarelyUsageList<int> list;
        int_handle one = list.push(1);
        int_handle two = list.push(2);
        int_handle three = list.push(3);
        CHECK(list.touch(one));
        CHECK(list.to_vector() == ints({1, 3, 2}));
        CHECK(*list.most_recent() == 1);
        CHECK(*list.least_recent() == 2);

        CHECK(list.touch(two));
        CHECK(list.to_vector() == ints({2, 1, 3}));
        CHECK(list.validate());

        // 已在最近端的句柄再次 touch 仍返回 true，顺序不变。
        CHECK(list.touch(two));
        CHECK(list.to_vector() == ints({2, 1, 3}));

        CHECK(list.touch(three));
        CHECK(list.to_vector() == ints({3, 2, 1}));

        // 默认句柄与越界索引都不属于本表。
        // 注意：句柄只含槽位索引与代际，不携带池身份，跨容器句柄不在语义保证范围内。
        const int_handle invalid{};
        const int_handle out_of_range{1000, 0};
        CHECK(!list.touch(invalid));
        CHECK(!list.erase(invalid));
        CHECK(!list.touch(out_of_range));
        CHECK(!list.erase(out_of_range));
        CHECK(list.value(invalid) == nullptr);
        CHECK(list.value(out_of_range) == nullptr);
        CHECK(list.size() == 3);
        CHECK(list.validate());

        LeastRarelyUsageList<int> other;
        int_handle foreign = other.push(42);
        CHECK(other.size() == 1);
        CHECK(*other.value(foreign) == 42);
        CHECK(other.validate());
    }

    // 容量满时淘汰最久未使用；池槽位复用，存活节点数不超过容量。
    {
        LeastRarelyUsageList<int> list(3);
        CHECK(list.capacity() == 3);
        int_handle one = list.push(1);
        list.push(2);
        list.push(3);
        CHECK(list.size() == 3);
        CHECK(list.pool_alive() == 3);

        int_handle four = list.push(4);
        CHECK(list.value(four) != nullptr && *list.value(four) == 4);
        CHECK(list.to_vector() == ints({4, 3, 2}));
        CHECK(list.value(one) == nullptr);
        CHECK(!list.erase(one));
        CHECK(!list.touch(one));
        CHECK(list.size() == 3);
        CHECK(list.validate());

        for (int i = 5; i <= 20; ++i)
        {
            list.push(i);
        }

        CHECK(list.size() == 3);
        CHECK(list.to_vector() == ints({20, 19, 18}));
        CHECK(list.pool_alive() == 3);
        CHECK(list.value(one) == nullptr);
        CHECK(list.value(four) == nullptr);
        CHECK(list.validate());

        // 容量为 1 的边界。
        LeastRarelyUsageList<int> single(1);
        int_handle first = single.push(1);
        int_handle second = single.push(2);
        CHECK(single.value(first) == nullptr);
        CHECK(single.value(second) != nullptr && *single.value(second) == 2);
        CHECK(single.size() == 1);
        CHECK(single.pool_alive() == 1);
        CHECK(!single.erase(first));
        CHECK(*single.most_recent() == 2);
        CHECK(*single.least_recent() == 2);
        CHECK(single.validate());
    }

    // set_capacity 缩小容量时按 LRU 淘汰；0 表示恢复不限容量。
    {
        LeastRarelyUsageList<int> list(5);
        std::vector<int_handle> handles;
        for (int i = 1; i <= 5; ++i)
        {
            handles.push_back(list.push(i));
        }

        CHECK(list.to_vector() == ints({5, 4, 3, 2, 1}));
        list.set_capacity(2);
        CHECK(list.capacity() == 2);
        CHECK(list.to_vector() == ints({5, 4}));
        CHECK(list.pool_alive() == 2);
        CHECK(list.validate());

        // 被淘汰的 1、2、3 对应句柄失效；保留的 4、5 仍然有效。
        CHECK(list.value(handles[0]) == nullptr);
        CHECK(list.value(handles[1]) == nullptr);
        CHECK(list.value(handles[2]) == nullptr);
        CHECK(!list.touch(handles[0]));
        CHECK(!list.erase(handles[1]));
        CHECK(list.value(handles[3]) != nullptr && *list.value(handles[3]) == 4);
        CHECK(list.value(handles[4]) != nullptr && *list.value(handles[4]) == 5);

        list.set_capacity(0);
        CHECK(list.capacity() == 0);
        for (int i = 6; i <= 9; ++i)
        {
            list.push(i);
        }

        CHECK(list.size() == 6);
        CHECK(list.to_vector() == ints({9, 8, 7, 6, 5, 4}));
        CHECK(list.pool_alive() == 6);
        CHECK(list.value(handles[0]) == nullptr);
        CHECK(list.validate());

        list.set_capacity(4);
        CHECK(list.to_vector() == ints({9, 8, 7, 6}));
        CHECK(list.pool_alive() == 4);
        CHECK(list.value(handles[3]) == nullptr);
        CHECK(list.value(handles[4]) == nullptr);
        CHECK(list.validate());

        // 容量扩大不淘汰任何元素。
        list.set_capacity(10);
        CHECK(list.size() == 4);
        CHECK(list.validate());
    }

    // erase 头 / 尾 / 中间；句柄失效后重复操作返回 false。
    {
        LeastRarelyUsageList<int> list;
        std::vector<int_handle> handles;
        for (int i = 1; i <= 5; ++i)
        {
            handles.push_back(list.push(i));
        }

        CHECK(list.to_vector() == ints({5, 4, 3, 2, 1}));
        CHECK(list.erase(handles[4]));
        CHECK(*list.most_recent() == 4);
        CHECK(list.value(handles[4]) == nullptr);
        CHECK(list.erase(handles[0]));
        CHECK(*list.least_recent() == 2);
        CHECK(list.value(handles[0]) == nullptr);
        CHECK(list.erase(handles[2]));
        CHECK(list.to_vector() == ints({4, 2}));
        CHECK(list.size() == 2);
        CHECK(list.pool_alive() == 2);
        CHECK(!list.erase(handles[2]));
        CHECK(!list.touch(handles[2]));
        CHECK(list.value(handles[2]) == nullptr);
        CHECK(list.validate());

        CHECK(list.erase(handles[1]));
        CHECK(list.erase(handles[3]));
        CHECK(list.empty());
        CHECK(list.pool_alive() == 0);
        CHECK(list.to_vector().empty());
        CHECK(list.validate());
        CHECK(!list.erase(handles[0]));
        CHECK(!list.touch(handles[4]));
        for (const int_handle& handle : handles)
        {
            CHECK(list.value(handle) == nullptr);
        }
    }

    // 核心回归：erase 后同槽位被新 push 复用，旧句柄必须被识别为失效。
    {
        LeastRarelyUsageList<int> list;
        int_handle hA = list.push(11);
        CHECK(list.value(hA) != nullptr && *list.value(hA) == 11);
        CHECK(list.erase(hA));
        CHECK(list.value(hA) == nullptr);
        CHECK(!list.touch(hA));
        CHECK(!list.erase(hA));

        int_handle hB = list.push(22);
        CHECK(hB.index == hA.index);           // 同一槽位被复用。
        CHECK(hB.generation != hA.generation); // 但代际不同，旧句柄失效。
        CHECK(list.value(hB) != nullptr && *list.value(hB) == 22);
        CHECK(list.value(hA) == nullptr);
        CHECK(!list.touch(hA));
        CHECK(!list.erase(hA));
        CHECK(list.size() == 1);
        CHECK(list.pool_alive() == 1);
        CHECK(*list.most_recent() == 22);
        CHECK(*list.least_recent() == 22);
        CHECK(list.validate());

        // 继续压缩大量元素让槽位反复复用，旧句柄 hA 仍然失效，hB 不受影响。
        for (int i = 0; i < 100; ++i)
        {
            list.push(i);
        }

        CHECK(list.value(hA) == nullptr);
        CHECK(!list.touch(hA));
        CHECK(!list.erase(hA));
        CHECK(list.value(hB) != nullptr && *list.value(hB) == 22);

        // 但 erase / clear 后 hB 也失效。
        CHECK(list.erase(hB));
        CHECK(list.value(hB) == nullptr);
        CHECK(!list.touch(hB));
        CHECK(!list.erase(hB));
        CHECK(list.validate());

        list.clear();
        CHECK(list.value(hA) == nullptr);
        CHECK(list.value(hB) == nullptr);
        CHECK(!list.touch(hA));
        CHECK(!list.erase(hB));
        CHECK(list.validate());
    }

    // clear 释放所有节点，池 alive() 归零，哨兵复位；之后可继续使用，旧句柄全部失效。
    {
        LeastRarelyUsageList<int> list(4);
        std::vector<int_handle> handles;
        for (int i = 0; i < 4; ++i)
        {
            handles.push_back(list.push(i));
        }

        CHECK(list.pool_alive() == 4);
        list.clear();
        CHECK(list.empty());
        CHECK(list.size() == 0);
        CHECK(list.pool_alive() == 0);
        CHECK(list.least_recent() == nullptr);
        CHECK(list.most_recent() == nullptr);
        CHECK(list.to_vector().empty());
        CHECK(list.validate());
        for (const int_handle& handle : handles)
        {
            CHECK(list.value(handle) == nullptr);
            CHECK(!list.touch(handle));
            CHECK(!list.erase(handle));
        }

        int_handle again = list.push(9);
        CHECK(list.value(again) != nullptr && *list.value(again) == 9);
        CHECK(list.size() == 1);
        CHECK(list.pool_alive() == 1);
        CHECK(list.validate());
        for (const int_handle& handle : handles)
        {
            CHECK(list.value(handle) == nullptr);
            CHECK(!list.touch(handle));
        }

        list.clear();
        CHECK(list.pool_alive() == 0);
        CHECK(list.value(again) == nullptr);
        CHECK(!list.touch(again));
    }

    // T 按值拷贝：修改源对象不影响表内值；句柄在后续 push 后依然有效。
    {
        LeastRarelyUsageList<std::string> list;
        std::string source = "alpha";
        string_handle handle = list.push(source);
        source[0] = 'X';
        CHECK(list.value(handle) != nullptr && *list.value(handle) == "alpha");
        string_handle beta = list.push("beta");
        CHECK(list.value(beta) != nullptr && *list.value(beta) == "beta");
        CHECK(list.touch(handle));
        CHECK(list.to_vector() == strings({"alpha", "beta"}));
        CHECK(list.validate());
        list.clear();
        CHECK(list.pool_alive() == 0);
        CHECK(list.value(handle) == nullptr);
        CHECK(!list.touch(handle));
        CHECK(list.validate());

        // 再压入 200 个元素，池扩容后旧句柄仍有效（槽位地址稳定 + 代际未变）。
        LeastRarelyUsageList<int> numbers;
        std::vector<int_handle> handles;
        handles.reserve(200);
        for (int i = 0; i < 200; ++i)
        {
            handles.push_back(numbers.push(i));
        }

        CHECK(numbers.size() == 200);
        CHECK(numbers.pool_alive() == 200);
        CHECK(numbers.value(handles[0]) != nullptr && *numbers.value(handles[0]) == 0);
        CHECK(numbers.value(handles[50]) != nullptr && *numbers.value(handles[50]) == 50);
        CHECK(numbers.value(handles[199]) != nullptr && *numbers.value(handles[199]) == 199);
        CHECK(numbers.touch(handles[0]));
        CHECK(numbers.most_recent() == numbers.value(handles[0]));
        CHECK(*numbers.most_recent() == 0);
        CHECK(*numbers.least_recent() == 1);
        CHECK(numbers.validate());
    }

    // T 不可默认构造：哨兵按需构造，容器照常可用，且池内对象全部析构。
    {
        CHECK(Tracked::alive_count == 0);

        {
            LeastRarelyUsageList<Tracked> list(3);
            Tracked one(1);
            Tracked two(2);
            Tracked three(3);
            Tracked four(4);
            CHECK(Tracked::alive_count == 4);

            auto handle = list.push(one);
            list.push(two);
            list.push(three);
            CHECK(list.value(handle) != nullptr);
            CHECK(list.value(handle)->value == 1);
            // 4 个局部变量 + 1 个哨兵占位副本 + 3 个数据节点。
            CHECK(Tracked::alive_count == 8);
            CHECK(list.pool_alive() == 3);
            CHECK(list.validate());

            CHECK(list.touch(handle));
            CHECK(list.most_recent() == list.value(handle));
            CHECK(list.least_recent()->value == 2);

            // 容量满：淘汰最久未使用的 2，同时新建 4。
            list.push(four);
            CHECK(list.size() == 3);
            CHECK(list.most_recent()->value == 4);
            CHECK(list.least_recent()->value == 3);
            CHECK(list.value(handle) != nullptr && list.value(handle)->value == 1);
            CHECK(Tracked::alive_count == 8);
            CHECK(list.pool_alive() == 3);
            CHECK(list.validate());

            list.clear();
            // 数据节点与哨兵全部析构，只剩 4 个局部变量。
            CHECK(Tracked::alive_count == 4);
            CHECK(list.pool_alive() == 0);
            CHECK(list.value(handle) == nullptr);
            CHECK(!list.touch(handle));
            CHECK(list.validate());

            auto again = list.push(four);
            CHECK(Tracked::alive_count == 6);
            CHECK(list.value(again) != nullptr && list.value(again)->value == 4);
            CHECK(list.erase(again));
            // 4 个局部变量 + 哨兵。
            CHECK(Tracked::alive_count == 5);
            CHECK(list.pool_alive() == 0);
            CHECK(list.validate());
            list.clear();
        }

        CHECK(Tracked::alive_count == 0);
    }

    // 2000 步随机操作（push / touch / erase / set_capacity）与 std::list 模型对照。
    // 每个被 erase / 淘汰的句柄都保留在 stale 中，之后每一步都必须保持失效，
    // 即使其槽位已经被新 push 复用。
    {
        std::mt19937 generator(20241109u);
        std::uniform_int_distribution<int> operation_distribution(0, 3);
        std::uniform_int_distribution<int> value_distribution(0, 999);
        std::uniform_int_distribution<int> capacity_distribution(0, 24);

        LeastRarelyUsageList<int> list;
        std::list<int> model;
        std::vector<int_handle> handles;
        std::vector<int_handle> stale;
        bool slot_reuse_observed = false;

        for (int step = 0; step < 2000; ++step)
        {
            int operation = operation_distribution(generator);
            if (model.empty() && operation != 0)
            {
                operation = 0;
            }

            if (operation == 0)
            {
                const int value = value_distribution(generator);
                int_handle handle = list.push(value);
                for (const int_handle& expired : stale)
                {
                    if (expired.index == handle.index)
                    {
                        slot_reuse_observed = true;
                        break;
                    }
                }

                if (list.capacity() != 0 && model.size() >= list.capacity())
                {
                    stale.push_back(handles.back());
                    model.pop_back();
                    handles.pop_back();
                }

                model.push_front(value);
                handles.insert(handles.begin(), handle);
            }
            else if (operation == 1)
            {
                std::uniform_int_distribution<std::size_t> index_distribution(0, model.size() - 1);
                const std::size_t index = index_distribution(generator);
                std::list<int>::iterator position = model.begin();
                std::advance(position, static_cast<std::ptrdiff_t>(index));

                CHECK(list.touch(handles[index]));
                model.splice(model.begin(), model, position);

                int_handle moved = handles[index];
                handles.erase(handles.begin() + static_cast<std::ptrdiff_t>(index));
                handles.insert(handles.begin(), moved);
            }
            else if (operation == 2)
            {
                std::uniform_int_distribution<std::size_t> index_distribution(0, model.size() - 1);
                const std::size_t index = index_distribution(generator);
                int_handle handle = handles[index];

                CHECK(list.erase(handle));
                CHECK(list.value(handle) == nullptr);
                CHECK(!list.erase(handle));
                CHECK(!list.touch(handle));
                stale.push_back(handle);

                std::list<int>::iterator position = model.begin();
                std::advance(position, static_cast<std::ptrdiff_t>(index));
                model.erase(position);
                handles.erase(handles.begin() + static_cast<std::ptrdiff_t>(index));
            }
            else
            {
                const std::size_t capacity = static_cast<std::size_t>(capacity_distribution(generator));
                list.set_capacity(capacity);
                while (capacity != 0 && model.size() > capacity)
                {
                    stale.push_back(handles.back());
                    model.pop_back();
                    handles.pop_back();
                }

                CHECK(list.capacity() == capacity);
            }

            CHECK(list.size() == model.size());
            CHECK(list.pool_alive() == model.size());
            CHECK(list.to_vector() == values_of(model));
            CHECK(list.validate());

            if (model.empty())
            {
                CHECK(list.most_recent() == nullptr);
                CHECK(list.least_recent() == nullptr);
            }
            else
            {
                CHECK(list.value(handles.front()) == list.most_recent());
                CHECK(list.value(handles.back()) == list.least_recent());
                CHECK(*list.most_recent() == model.front());
                CHECK(*list.least_recent() == model.back());
            }

            for (const int_handle& expired : stale)
            {
                CHECK(list.value(expired) == nullptr);
                CHECK(!list.touch(expired));
                CHECK(!list.erase(expired));
            }
        }

        CHECK(!stale.empty());
        CHECK(slot_reuse_observed);
    }

    std::cout << "PASS: lru_list" << std::endl;
    return 0;
}
