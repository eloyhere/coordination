// 带 LRU 淘汰的无序集合测试：
// 覆盖空集、插入去重与顺序、touch/find/contains、容量淘汰、set_capacity 缩小、
// erase 首/中/尾、rehash 后顺序、clear 回收对象池，以及 mt19937(7) 的 3000 步
// 随机差分（对照 std::unordered_set 与 LRU 顺序模型）。
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <list>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "structure/lru/LeastRarelyUsageUnorderedSet.h"

namespace
{
    using Set = coordination::lru::LeastRarelyUsageUnorderedSet<int>;

    int g_failures = 0;

    void expect(bool condition, const std::string& message)
    {
        if (!condition)
        {
            ++g_failures;
            if (g_failures <= 20)
            {
                std::printf("FAIL: %s\n", message.c_str());
            }
        }
    }

    std::string at_step(const char* name, const char* what, int step)
    {
        return std::string(name) + ": " + what + " @" + std::to_string(step);
    }

    std::vector<int> order_vector(const std::list<int>& order)
    {
        return std::vector<int>(order.begin(), order.end());
    }

    // 把 value 提升到模型头部（最近使用）。
    void model_touch(std::list<int>& order, int value)
    {
        const auto it = std::find(order.begin(), order.end(), value);
        if (it != order.end())
        {
            order.splice(order.begin(), order, it);
        }
    }

    void model_erase(std::list<int>& order, int value)
    {
        const auto it = std::find(order.begin(), order.end(), value);
        if (it != order.end())
        {
            order.erase(it);
        }
    }

    void check_empty()
    {
        Set set;

        expect(set.empty(), "empty: 新集合应为空");
        expect(set.size() == 0, "empty: 新集合 size 应为 0");
        expect(set.capacity() == 0, "empty: 默认容量应为 0（不限）");
        expect(set.find(7) == nullptr, "empty: find 应返回 nullptr");
        expect(!set.contains(7), "empty: contains 应返回 false");
        expect(!set.touch(7), "empty: touch 应返回 false");
        expect(!set.erase(7), "empty: erase 应返回 false");
        expect(!set.evict_one(), "empty: evict_one 应返回 false");
        expect(set.to_vector().empty(), "empty: to_vector 应为空");
        expect(set.validate(), "empty: validate 应为 true");
        expect(set.pool_alive() == 0, "empty: 池存活数应为 0");

        set.clear();
        expect(set.validate(), "empty: clear 后 validate 应为 true");
        expect(set.pool_alive() == 0, "empty: clear 后池存活数应为 0");
    }

    void check_insert_order()
    {
        Set set;

        expect(set.insert(10), "insert: 首次插入 10 应返回 true");
        expect(set.insert(20), "insert: 首次插入 20 应返回 true");
        expect(set.insert(30), "insert: 首次插入 30 应返回 true");
        expect(set.to_vector() == std::vector<int>{30, 20, 10}, "insert: 顺序应为最近使用在前");

        expect(!set.insert(20), "insert: 重复插入 20 应返回 false");
        expect(set.size() == 3, "insert: 重复插入后 size 仍应为 3");
        expect(set.to_vector() == std::vector<int>{20, 30, 10}, "insert: 重复插入应提升为最近使用");

        expect(set.contains(10), "insert: contains(10) 应为 true");
        expect(!set.contains(99), "insert: contains(99) 应为 false");
        const int* found = set.find(10);
        expect(found != nullptr && *found == 10, "insert: find(10) 应命中且值正确");
        expect(set.find(99) == nullptr, "insert: find(99) 应为 nullptr");
        expect(set.to_vector() == std::vector<int>{20, 30, 10}, "insert: find/contains 不应改变 LRU 顺序");

        expect(set.pool_alive() == 3, "insert: 池存活数应等于 size");
        expect(set.validate(), "insert: validate 应为 true");
    }

    void check_touch()
    {
        Set set;
        set.insert(1);
        set.insert(2);
        set.insert(3);

        expect(set.to_vector() == std::vector<int>{3, 2, 1}, "touch: 初始顺序应为 3,2,1");
        expect(set.touch(1), "touch: 提升最久未使用的 1 应返回 true");
        expect(set.to_vector() == std::vector<int>{1, 3, 2}, "touch: 1 应变为最近使用");
        expect(set.touch(1), "touch: 重复提升最近的 1 应返回 true");
        expect(set.to_vector() == std::vector<int>{1, 3, 2}, "touch: 重复提升不应改变顺序");
        expect(!set.touch(4), "touch: 提升不存在的 4 应返回 false");
        expect(set.size() == 3, "touch: 不应改变 size");
        expect(set.validate(), "touch: validate 应为 true");
    }

    void check_capacity()
    {
        Set set(3);
        expect(set.capacity() == 3, "capacity: capacity() 应为 3");

        set.insert(1);
        set.insert(2);
        set.insert(3);
        expect(set.to_vector() == std::vector<int>{3, 2, 1}, "capacity: 初始顺序应为 3,2,1");

        expect(!set.insert(3), "capacity: 已满时重复插入应返回 false");
        expect(set.to_vector() == std::vector<int>{3, 2, 1}, "capacity: 重复插入不应触发淘汰");
        expect(set.size() == 3, "capacity: 重复插入后 size 不应变化");

        expect(set.insert(4), "capacity: 插入新元素 4 应返回 true");
        expect(set.to_vector() == std::vector<int>{4, 3, 2}, "capacity: 应淘汰最久未使用的 1");
        expect(!set.contains(1), "capacity: 被淘汰的 1 应不存在");
        expect(set.size() == 3, "capacity: 淘汰后 size 应保持 3");

        expect(set.insert(5), "capacity: 插入新元素 5 应返回 true");
        expect(set.to_vector() == std::vector<int>{5, 4, 3}, "capacity: 应继续淘汰最久未使用的 2");
        expect(!set.contains(2), "capacity: 被淘汰的 2 应不存在");

        set.set_capacity(1);
        expect(set.capacity() == 1, "capacity: set_capacity(1) 后 capacity() 应为 1");
        expect(set.size() == 1, "capacity: 缩容后 size 应变为 1");
        expect(set.to_vector() == std::vector<int>{5}, "capacity: 缩容后应保留最近的 5");
        expect(set.validate(), "capacity: 缩容后 validate 应为 true");

        set.set_capacity(0);
        set.insert(6);
        set.insert(7);
        expect(set.to_vector() == std::vector<int>{7, 6, 5}, "capacity: 取消限制后应保留全部元素");

        set.set_capacity(2);
        expect(set.to_vector() == std::vector<int>{7, 6}, "capacity: 再次缩容应淘汰最久未使用的 5");
        expect(!set.contains(5), "capacity: 再次缩容后 5 应不存在");
        expect(set.validate(), "capacity: 再次缩容后 validate 应为 true");
    }

    void check_set_capacity()
    {
        Set set;
        for (int value = 1; value <= 6; ++value)
        {
            set.insert(value);
        }
        expect(set.capacity() == 0, "set_capacity: 默认容量应为 0");
        expect(set.size() == 6, "set_capacity: 初始 size 应为 6");

        set.set_capacity(3);
        expect(set.capacity() == 3, "set_capacity: 缩小后 capacity() 应为 3");
        expect(set.size() == 3, "set_capacity: 缩小后 size 应为 3");
        expect(set.to_vector() == std::vector<int>{6, 5, 4}, "set_capacity: 应保留最近使用的 6,5,4");
        expect(!set.contains(3), "set_capacity: 最久未使用的 3 应被淘汰");

        set.set_capacity(5);
        expect(set.size() == 3, "set_capacity: 放大容量不应淘汰元素");
        expect(set.to_vector() == std::vector<int>{6, 5, 4}, "set_capacity: 放大容量不应改变顺序");

        set.insert(7);
        set.insert(8);
        expect(set.to_vector() == std::vector<int>{8, 7, 6, 5, 4}, "set_capacity: 放大容量后应能继续插入");
        expect(set.size() == 5, "set_capacity: 插入后 size 应为 5");

        set.set_capacity(0);
        set.insert(9);
        expect(set.size() == 6, "set_capacity: 取消限制后应可继续增长");
        expect(set.validate(), "set_capacity: validate 应为 true");
    }

    void check_erase()
    {
        Set set;
        for (int value = 1; value <= 4; ++value)
        {
            set.insert(value);
        }

        expect(set.erase(3), "erase: 删除中间元素 3 应返回 true");
        expect(set.to_vector() == std::vector<int>{4, 2, 1}, "erase: 删除中间元素后顺序应为 4,2,1");
        expect(set.erase(1), "erase: 删除最久未使用的 1 应返回 true");
        expect(set.to_vector() == std::vector<int>{4, 2}, "erase: 删除链尾后顺序应为 4,2");
        expect(set.erase(4), "erase: 删除最近使用的 4 应返回 true");
        expect(set.to_vector() == std::vector<int>{2}, "erase: 删除链头后应只剩 2");
        expect(set.erase(2), "erase: 删除最后一个元素应返回 true");
        expect(set.empty(), "erase: 删除全部后应为空");
        expect(set.size() == 0, "erase: 删除全部后 size 应为 0");
        expect(set.to_vector().empty(), "erase: 删除全部后 to_vector 应为空");
        expect(set.pool_alive() == 0, "erase: 删除全部后池存活数应为 0");
        expect(set.validate(), "erase: 删除全部后 validate 应为 true");
        expect(!set.evict_one(), "erase: 空集 evict_one 应返回 false");

        expect(!set.erase(2), "erase: 删除不存在的元素应返回 false");
        set.insert(5);
        expect(set.size() == 1, "erase: 清空后重新插入 size 应为 1");
        expect(set.to_vector() == std::vector<int>{5}, "erase: 清空后重新插入内容应正确");
        expect(set.pool_alive() == 1, "erase: 清空后重新插入池存活数应为 1");
        expect(set.validate(), "erase: 清空后重新插入 validate 应为 true");

        set.clear();
        expect(set.pool_alive() == 0, "erase: clear 后池存活数应为 0");
    }

    void check_rehash()
    {
        Set set;
        for (int value = 0; value < 6; ++value)
        {
            set.insert(value);
        }

        const std::vector<int> before = set.to_vector();
        expect(before == std::vector<int>{5, 4, 3, 2, 1, 0}, "rehash: 触发前顺序应为插入逆序");

        const int* stable = set.find(5);

        // 已有 6 个元素，桶数 8；插入第 7 个会超过 3/4 负载因子并触发桶翻倍。
        set.insert(6);
        expect(set.find(5) == stable, "rehash: 扩容不应使元素地址失效");
        const std::vector<int> after = set.to_vector();
        expect(after.size() == 7, "rehash: 插入后 size 应为 7");
        expect(after.front() == 6, "rehash: 新元素应位于最近使用端");
        expect(std::vector<int>(after.begin() + 1, after.end()) == before, "rehash: 不应改变已有元素的 LRU 顺序");
        expect(set.validate(), "rehash: 扩容后 validate 应为 true");

        Set large;
        std::list<int> order;
        bool all_inserted = true;
        for (int value = 0; value < 500; ++value)
        {
            if (!large.insert(value))
            {
                all_inserted = false;
            }
            order.push_front(value);

            if (value % 61 == 0)
            {
                expect(large.validate(), "rehash: 批量插入过程中 validate 失败");
            }
        }

        expect(all_inserted, "rehash: 500 个不同元素应全部插入成功");
        expect(large.size() == 500, "rehash: 批量插入后 size 应为 500");
        expect(large.to_vector() == order_vector(order), "rehash: 多次扩容后 LRU 顺序应保持");
        expect(large.validate(), "rehash: 多次扩容后 validate 应为 true");

        bool all_found = true;
        for (int value = 0; value < 500; ++value)
        {
            const int* found = large.find(value);
            if (found == nullptr || *found != value)
            {
                all_found = false;
            }
        }
        expect(all_found, "rehash: 多次扩容后所有元素仍应可查找");
        expect(large.contains(499), "rehash: contains 应能找到扩容前插入的元素");

        expect(large.touch(0), "rehash: touch(0) 应返回 true");
        model_touch(order, 0);
        expect(large.touch(250), "rehash: touch(250) 应返回 true");
        model_touch(order, 250);
        expect(large.to_vector() == order_vector(order), "rehash: 扩容后 touch 顺序应正确");

        expect(large.erase(499), "rehash: 删除最近使用的 499 应返回 true");
        model_erase(order, 499);
        expect(large.erase(1), "rehash: 删除中间位置的 1 应返回 true");
        model_erase(order, 1);
        expect(large.to_vector() == order_vector(order), "rehash: 扩容后 erase 顺序应正确");
        expect(large.validate(), "rehash: 扩容后 erase validate 应为 true");
    }

    void check_string_values()
    {
        using StringSet = coordination::lru::LeastRarelyUsageUnorderedSet<std::string>;

        StringSet set(2);
        expect(set.insert(std::string("alpha")), "string: 插入 alpha 应返回 true");
        expect(set.insert(std::string("beta")), "string: 插入 beta 应返回 true");
        expect(set.insert(std::string("gamma")), "string: 插入 gamma 应返回 true");
        expect(!set.contains(std::string("alpha")), "string: 容量淘汰后 alpha 应不存在");
        expect(set.to_vector() == std::vector<std::string>{"gamma", "beta"}, "string: 顺序应为 gamma,beta");

        const std::string* found = set.find(std::string("beta"));
        expect(found != nullptr && *found == "beta", "string: find(beta) 应命中");
        expect(set.validate(), "string: validate 应为 true");

        set.clear();
        expect(set.empty(), "string: clear 后应为空");
        expect(set.pool_alive() == 0, "string: clear 后池存活数应为 0");
    }

    // 只可拷贝、不可默认构造的值类型，同时验证自定义 Hash/Equal 模板参数。
    struct NoDefault
    {
        explicit NoDefault(int v)
            : value(v)
        {
        }

        int value = 0;
    };

    struct NoDefaultHash
    {
        std::size_t operator()(const NoDefault& value) const noexcept
        {
            return std::hash<int>{}(value.value);
        }
    };

    struct NoDefaultEqual
    {
        bool operator()(const NoDefault& left, const NoDefault& right) const noexcept
        {
            return left.value == right.value;
        }
    };

    void check_custom_type()
    {
        using CustomSet = coordination::lru::LeastRarelyUsageUnorderedSet<NoDefault, NoDefaultHash, NoDefaultEqual>;

        CustomSet set(2);
        expect(set.insert(NoDefault(1)), "custom: 插入 1 应返回 true");
        expect(set.insert(NoDefault(2)), "custom: 插入 2 应返回 true");
        expect(!set.insert(NoDefault(1)), "custom: 重复插入 1 应返回 false");
        expect(set.size() == 2, "custom: 重复插入后 size 应为 2");
        expect(set.to_vector().front().value == 1, "custom: 重复插入应提升 1 为最近使用");

        const NoDefault* found = set.find(NoDefault(2));
        expect(found != nullptr && found->value == 2, "custom: find(2) 应命中");
        expect(set.validate(), "custom: validate 应为 true");

        expect(set.insert(NoDefault(3)), "custom: 插入 3 应返回 true");
        expect(set.to_vector().front().value == 3, "custom: 3 应为最近使用");
        expect(!set.contains(NoDefault(2)), "custom: 容量淘汰后 2 应不存在");
        expect(set.validate(), "custom: 淘汰后 validate 应为 true");
    }

    // Capacity 为 0 表示不限容量；否则用固定容量与 LRU 模型同步淘汰。
    template <std::size_t Capacity>
    void check_random(const char* name)
    {
        Set set(Capacity);
        std::unordered_set<int> reference;
        std::list<int> order;

        std::mt19937 rng(7);
        std::uniform_int_distribution<int> value_dist(0, 255);
        std::uniform_int_distribution<int> op_dist(0, 99);

        for (int step = 0; step < 3000; ++step)
        {
            const int value = value_dist(rng);
            const int op = op_dist(rng);

            if (op < 45)
            {
                const bool expected = reference.find(value) == reference.end();
                const bool actual = set.insert(value);
                expect(actual == expected, at_step(name, "insert 返回值不一致", step));

                if (!expected)
                {
                    model_touch(order, value);
                }
                else
                {
                    if constexpr (Capacity != 0)
                    {
                        if (reference.size() >= Capacity)
                        {
                            expect(!order.empty(), at_step(name, "容量淘汰模型为空", step));
                            if (!order.empty())
                            {
                                const int victim = order.back();
                                reference.erase(victim);
                                order.pop_back();
                            }
                        }
                    }

                    reference.insert(value);
                    order.push_front(value);
                }
            }
            else if (op < 70)
            {
                const bool expected = reference.erase(value) != 0;
                const bool actual = set.erase(value);
                expect(actual == expected, at_step(name, "erase 返回值不一致", step));
                if (actual)
                {
                    model_erase(order, value);
                }
            }
            else if (op < 85)
            {
                const bool expected = reference.find(value) != reference.end();
                const bool actual = set.touch(value);
                expect(actual == expected, at_step(name, "touch 返回值不一致", step));
                if (expected)
                {
                    model_touch(order, value);
                }
            }
            else if (op < 95)
            {
                const bool expected = reference.find(value) != reference.end();
                expect(set.contains(value) == expected, at_step(name, "contains 不一致", step));

                const int* found = set.find(value);
                expect((found != nullptr) == expected, at_step(name, "find 命中与否不一致", step));
                if (found != nullptr)
                {
                    expect(*found == value, at_step(name, "find 返回的值不对", step));
                }
            }
            else
            {
                const bool expected = !reference.empty();
                const bool actual = set.evict_one();
                expect(actual == expected, at_step(name, "evict_one 返回值不一致", step));

                if (actual && !order.empty())
                {
                    const int victim = order.back();
                    expect(!set.contains(victim), at_step(name, "evict_one 没有淘汰最久未使用的元素", step));
                    reference.erase(victim);
                    order.pop_back();
                }
            }

            expect(set.size() == reference.size(), at_step(name, "size 与参考集合不一致", step));
            expect(set.validate(), at_step(name, "validate 失败", step));

            if (step % 32 == 0)
            {
                expect(set.to_vector() == order_vector(order), at_step(name, "LRU 顺序与模型不一致", step));
            }
        }

        expect(set.to_vector() == order_vector(order), std::string(name) + ": 结束时 LRU 顺序与模型不一致");
        expect(set.validate(), std::string(name) + ": 结束时 validate 应为 true");

        set.clear();
        expect(set.empty(), std::string(name) + ": clear 后应为空");
        expect(set.pool_alive() == 0, std::string(name) + ": clear 后池存活数应为 0");
        expect(set.validate(), std::string(name) + ": clear 后 validate 应为 true");
    }
}

int main()
{
    check_empty();
    check_insert_order();
    check_touch();
    check_capacity();
    check_set_capacity();
    check_erase();
    check_rehash();
    check_string_values();
    check_custom_type();
    check_random<0>("random-unlimited");
    check_random<64>("random-capacity");

    if (g_failures == 0)
    {
        std::printf("PASS: lru_unordered_set\n");
        return 0;
    }

    std::printf("FAILURES: %d\n", g_failures);
    return 1;
}
