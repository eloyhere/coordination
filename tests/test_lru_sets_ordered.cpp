#include "../structure/lru/LeaseRarelyUsageOrderedSet.h"

#include <cstddef>
#include <iostream>
#include <list>
#include <random>
#include <set>
#include <string>
#include <vector>

// 失败即打印原因并退出；测试全部通过时只输出一行 PASS。
#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cout << "FAIL: " << #condition << " (line " << __LINE__ << ")" << std::endl; \
            return 1; \
        } \
    } while (false)

using coordination::lru::LeaseRarelyUsageOrderedSet;

namespace
{
    std::vector<int> sorted_values(const std::set<int>& values)
    {
        return std::vector<int>(values.begin(), values.end());
    }

    std::vector<int> list_values(const std::list<int>& values)
    {
        return std::vector<int>(values.begin(), values.end());
    }

    void model_touch(std::list<int>& lru, int value)
    {
        lru.remove(value);
        lru.push_front(value);
    }

    // 与容器实现独立的参考模型：std::set 提供有序视图，std::list 头为最近使用。
    bool model_insert(std::set<int>& ordered, std::list<int>& lru, std::size_t capacity, int value)
    {
        if (ordered.find(value) != ordered.end())
        {
            model_touch(lru, value);
            return false;
        }

        if (capacity != 0 && ordered.size() >= capacity)
        {
            const int victim = lru.back();
            lru.pop_back();
            ordered.erase(victim);
        }

        ordered.insert(value);
        lru.push_front(value);
        return true;
    }

    bool model_erase(std::set<int>& ordered, std::list<int>& lru, int value)
    {
        if (ordered.erase(value) == 0)
        {
            return false;
        }

        lru.remove(value);
        return true;
    }

    bool model_evict(std::set<int>& ordered, std::list<int>& lru)
    {
        if (lru.empty())
        {
            return false;
        }

        const int victim = lru.back();
        lru.pop_back();
        ordered.erase(victim);
        return true;
    }
}

int main()
{
    // 空集：查询、淘汰、清空都应是安全空操作。
    {
        LeaseRarelyUsageOrderedSet<int> set;
        CHECK(set.empty());
        CHECK(set.size() == 0);
        CHECK(set.capacity() == 0);
        CHECK(set.pool_alive() == 0);
        CHECK(set.validate());
        CHECK(!set.contains(42));
        CHECK(!set.erase(42));
        CHECK(!set.touch(42));
        CHECK(set.find(42) == nullptr);
        CHECK(!set.evict_one());
        CHECK(set.to_vector().empty());
        CHECK(set.lru_order().empty());

        set.clear();
        CHECK(set.empty());
        CHECK(set.pool_alive() == 0);
        CHECK(set.validate());
    }

    // 插入、顺序视图、LRU 视图与重复插入。
    {
        LeaseRarelyUsageOrderedSet<int> set;
        CHECK(set.insert(30));
        CHECK(set.insert(10));
        CHECK(set.insert(20));
        CHECK(set.size() == 3);
        CHECK(set.to_vector() == std::vector<int>({10, 20, 30}));
        CHECK(set.lru_order() == std::vector<int>({20, 10, 30}));
        CHECK(set.pool_alive() == 3);
        CHECK(set.validate());

        // 重复插入不改变大小，只把已有元素提升为最近使用。
        CHECK(!set.insert(10));
        CHECK(set.size() == 3);
        CHECK(set.to_vector() == std::vector<int>({10, 20, 30}));
        CHECK(set.lru_order() == std::vector<int>({10, 20, 30}));
        CHECK(set.validate());

        const int* found = set.find(20);
        CHECK(found != nullptr);
        CHECK(*found == 20);
        CHECK(set.find(21) == nullptr);
        CHECK(set.contains(30));
        CHECK(!set.contains(31));

        CHECK(set.touch(30));
        CHECK(set.lru_order() == std::vector<int>({30, 10, 20}));
        CHECK(!set.touch(31));
        CHECK(set.validate());
    }

    // 容量已满时重复插入只提升顺序，不应触发淘汰。
    {
        LeaseRarelyUsageOrderedSet<int> set(2);
        CHECK(set.insert(1));
        CHECK(set.insert(2));
        CHECK(!set.insert(1));
        CHECK(set.size() == 2);
        CHECK(set.contains(2));
        CHECK(set.lru_order() == std::vector<int>({1, 2}));
        CHECK(set.validate());
    }

    // 容量满时淘汰最久未使用节点，再插入新元素。
    {
        LeaseRarelyUsageOrderedSet<int> set(3);
        CHECK(set.insert(1));
        CHECK(set.insert(2));
        CHECK(set.insert(3));
        CHECK(set.to_vector() == std::vector<int>({1, 2, 3}));
        CHECK(set.lru_order() == std::vector<int>({3, 2, 1}));

        CHECK(set.insert(4));
        CHECK(!set.contains(1));
        CHECK(set.size() == 3);
        CHECK(set.to_vector() == std::vector<int>({2, 3, 4}));
        CHECK(set.lru_order() == std::vector<int>({4, 3, 2}));
        CHECK(set.validate());

        CHECK(set.touch(2));
        CHECK(set.insert(5));
        CHECK(!set.contains(3));
        CHECK(set.to_vector() == std::vector<int>({2, 4, 5}));
        CHECK(set.lru_order() == std::vector<int>({5, 2, 4}));
        CHECK(set.pool_alive() == 3);
        CHECK(set.validate());
    }

    // set_capacity：缩小时从最久未使用端淘汰，0 恢复不限容量。
    {
        LeaseRarelyUsageOrderedSet<int> set;
        for (int value = 0; value < 5; ++value)
        {
            CHECK(set.insert(value));
        }

        CHECK(set.capacity() == 0);
        CHECK(set.lru_order() == std::vector<int>({4, 3, 2, 1, 0}));

        set.set_capacity(2);
        CHECK(set.capacity() == 2);
        CHECK(set.size() == 2);
        CHECK(set.to_vector() == std::vector<int>({3, 4}));
        CHECK(set.lru_order() == std::vector<int>({4, 3}));
        CHECK(set.pool_alive() == 2);
        CHECK(set.validate());

        CHECK(set.insert(9));
        CHECK(!set.contains(3));
        CHECK(set.to_vector() == std::vector<int>({4, 9}));
        CHECK(set.lru_order() == std::vector<int>({9, 4}));
        CHECK(set.validate());

        set.set_capacity(0);
        CHECK(set.capacity() == 0);
        CHECK(set.insert(10));
        CHECK(set.insert(11));
        CHECK(set.size() == 4);
        CHECK(set.validate());
        CHECK(set.pool_alive() == 4);
    }

    // 删除 LRU 链的头、中间与尾部节点，两处索引同步。
    {
        LeaseRarelyUsageOrderedSet<int> set;
        for (int value = 10; value <= 50; value += 10)
        {
            CHECK(set.insert(value));
        }
        CHECK(set.lru_order() == std::vector<int>({50, 40, 30, 20, 10}));
        CHECK(set.pool_alive() == 5);

        CHECK(set.erase(50));
        CHECK(set.lru_order() == std::vector<int>({40, 30, 20, 10}));
        CHECK(set.to_vector() == std::vector<int>({10, 20, 30, 40}));
        CHECK(set.pool_alive() == 4);
        CHECK(set.validate());

        CHECK(set.erase(30));
        CHECK(set.lru_order() == std::vector<int>({40, 20, 10}));
        CHECK(set.to_vector() == std::vector<int>({10, 20, 40}));
        CHECK(set.pool_alive() == 3);
        CHECK(set.validate());

        CHECK(set.erase(10));
        CHECK(set.lru_order() == std::vector<int>({40, 20}));
        CHECK(set.to_vector() == std::vector<int>({20, 40}));
        CHECK(set.pool_alive() == 2);
        CHECK(set.validate());

        CHECK(!set.erase(10));
        CHECK(!set.erase(1000));

        CHECK(set.evict_one());
        CHECK(set.lru_order() == std::vector<int>({40}));
        CHECK(set.evict_one());
        CHECK(set.empty());
        CHECK(set.pool_alive() == 0);
        CHECK(!set.evict_one());
        CHECK(set.validate());
    }

    // clear 回收对象池中的全部节点，容量保留，之后还能继续使用。
    {
        LeaseRarelyUsageOrderedSet<int> set(4);
        for (int value = 0; value < 4; ++value)
        {
            CHECK(set.insert(value));
        }
        CHECK(set.pool_alive() == 4);

        set.clear();
        CHECK(set.empty());
        CHECK(set.size() == 0);
        CHECK(set.capacity() == 4);
        CHECK(set.pool_alive() == 0);
        CHECK(set.to_vector().empty());
        CHECK(set.lru_order().empty());
        CHECK(set.validate());

        CHECK(set.insert(7));
        CHECK(set.insert(8));
        CHECK(set.pool_alive() == 2);
        CHECK(set.lru_order() == std::vector<int>({8, 7}));
        CHECK(set.validate());
    }

    // 非平凡值类型：验证对象池中的节点能正确构造、复制与析构。
    {
        LeaseRarelyUsageOrderedSet<std::string> set(2);
        CHECK(set.insert("alpha"));
        CHECK(set.insert("beta"));
        CHECK(set.to_vector() == std::vector<std::string>({"alpha", "beta"}));
        CHECK(set.lru_order() == std::vector<std::string>({"beta", "alpha"}));

        CHECK(!set.insert("alpha"));
        CHECK(set.lru_order() == std::vector<std::string>({"alpha", "beta"}));

        CHECK(set.insert("gamma"));
        CHECK(!set.contains("beta"));
        CHECK(set.find("gamma") != nullptr);
        CHECK(set.validate());
        CHECK(set.pool_alive() == 2);

        set.clear();
        CHECK(set.pool_alive() == 0);
        CHECK(set.validate());
    }

    // 固定种子随机差分：与 std::set + 手工 LRU 模型逐步对照。
    {
        constexpr std::size_t capacity = 64;
        LeaseRarelyUsageOrderedSet<int> set(capacity);
        std::set<int> ordered;
        std::list<int> lru;
        std::mt19937 generator(11);
        std::uniform_int_distribution<int> value_distribution(-200, 200);
        std::uniform_int_distribution<int> operation_distribution(0, 5);

        for (int step = 0; step < 3000; ++step)
        {
            const int value = value_distribution(generator);
            switch (operation_distribution(generator))
            {
            case 0:
                CHECK(set.insert(value) == model_insert(ordered, lru, capacity, value));
                break;
            case 1:
                CHECK(set.erase(value) == model_erase(ordered, lru, value));
                break;
            case 2:
            {
                const bool expected = ordered.find(value) != ordered.end();
                CHECK(set.touch(value) == expected);
                if (expected)
                {
                    model_touch(lru, value);
                }
                break;
            }
            case 3:
            {
                const bool expected = ordered.find(value) != ordered.end();
                CHECK(set.contains(value) == expected);
                break;
            }
            case 4:
            {
                const bool expected = ordered.find(value) != ordered.end();
                const int* found = set.find(value);
                CHECK((found != nullptr) == expected);
                if (found != nullptr)
                {
                    CHECK(*found == value);
                }
                break;
            }
            default:
                CHECK(set.evict_one() == model_evict(ordered, lru));
                break;
            }

            CHECK(set.validate());
            CHECK(set.size() == ordered.size());
            CHECK(set.pool_alive() == ordered.size());

            if (step % 17 == 0)
            {
                CHECK(set.to_vector() == sorted_values(ordered));
                CHECK(set.lru_order() == list_values(lru));
            }
        }

        CHECK(set.to_vector() == sorted_values(ordered));
        CHECK(set.lru_order() == list_values(lru));
        CHECK(set.validate());

        set.clear();
        CHECK(set.pool_alive() == 0);
        CHECK(set.validate());
    }

    std::cout << "PASS: lru_ordered_set" << std::endl;
    return 0;
}
