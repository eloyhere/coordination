// BinarySearchTree 自包含测试：不依赖任何第三方测试框架。
// 覆盖空树、插入/重复插入、各类删除、查询边界、与 std::set 的差分随机对照、
// 高度与结构自检。全部通过时返回 0，否则打印 FAIL 并返回 1。

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <random>
#include <set>
#include <vector>

#include "structure/tree/BinarySearchTree.h"

namespace
{
    // 统一断言入口：失败时打印 "FAIL: 表达式 (行号)"。
    bool expect(bool condition, const char* expression, int line)
    {
        if (!condition)
        {
            std::cout << "FAIL: " << expression << " (line " << line << ")\n";
        }

        return condition;
    }
}

// 只能作为独立语句使用；失败时从当前测试函数返回 false。
#define CHECK(condition) \
    if (!expect((condition), #condition, __LINE__)) \
    { \
        return false; \
    }

namespace
{
    using coordination::tree::BinarySearchTree;

    bool test_empty_tree()
    {
        BinarySearchTree<int> tree;
        CHECK(tree.empty());
        CHECK(tree.size() == 0u);
        CHECK(tree.height() == 0);
        CHECK(tree.validate());

        CHECK(!tree.contains(42));
        CHECK(tree.find(42) == nullptr);
        CHECK(tree.min() == nullptr);
        CHECK(tree.max() == nullptr);
        CHECK(tree.floor(42) == nullptr);
        CHECK(tree.ceil(42) == nullptr);
        CHECK(tree.to_vector().empty());

        CHECK(!tree.erase(42));
        CHECK(tree.size() == 0u);

        tree.clear();
        CHECK(tree.empty());
        CHECK(tree.validate());
        return true;
    }

    bool test_insert_and_duplicates()
    {
        BinarySearchTree<int> tree;
        CHECK(tree.insert(50));
        CHECK(tree.insert(30));
        CHECK(tree.insert(70));
        CHECK(tree.insert(20));
        CHECK(tree.insert(40));
        CHECK(tree.insert(60));
        CHECK(tree.insert(80));
        CHECK(tree.size() == 7u);

        // 重复插入（含根、叶子）必须失败且不改变元素集合。
        CHECK(!tree.insert(50));
        CHECK(!tree.insert(30));
        CHECK(!tree.insert(80));
        CHECK(tree.size() == 7u);

        const std::vector<int> expected{20, 30, 40, 50, 60, 70, 80};
        CHECK(tree.to_vector() == expected);
        CHECK(tree.height() == 3);
        CHECK(tree.validate());
        return true;
    }

    bool test_erase_shapes()
    {
        // 删除叶子节点。
        {
            BinarySearchTree<int> tree;
            for (int value : {50, 30, 70, 20, 40})
            {
                CHECK(tree.insert(value));
            }

            CHECK(tree.erase(20));
            CHECK(!tree.contains(20));
            CHECK(tree.size() == 4u);
            const std::vector<int> expected{30, 40, 50, 70};
            CHECK(tree.to_vector() == expected);
            CHECK(tree.validate());
        }

        // 删除只有左孩子的节点。
        {
            BinarySearchTree<int> tree;
            for (int value : {50, 30, 20})
            {
                CHECK(tree.insert(value));
            }

            CHECK(tree.erase(30));
            CHECK(tree.size() == 2u);
            const std::vector<int> expected{20, 50};
            CHECK(tree.to_vector() == expected);
            CHECK(tree.validate());
        }

        // 删除只有右孩子的节点。
        {
            BinarySearchTree<int> tree;
            for (int value : {50, 30, 40})
            {
                CHECK(tree.insert(value));
            }

            CHECK(tree.erase(30));
            CHECK(tree.size() == 2u);
            const std::vector<int> expected{40, 50};
            CHECK(tree.to_vector() == expected);
            CHECK(tree.validate());
        }

        // 删除有两个孩子的节点：中序后继 40 覆盖 30。
        {
            BinarySearchTree<int> tree;
            for (int value : {50, 30, 70, 20, 40, 60, 80})
            {
                CHECK(tree.insert(value));
            }

            CHECK(tree.erase(30));
            CHECK(!tree.contains(30));
            CHECK(tree.contains(40));
            CHECK(tree.size() == 6u);
            const std::vector<int> expected{20, 40, 50, 60, 70, 80};
            CHECK(tree.to_vector() == expected);
            CHECK(tree.validate());
        }

        // 删除有两个孩子的根：中序后继 60 接替根。
        {
            BinarySearchTree<int> tree;
            for (int value : {50, 30, 70, 20, 40, 60, 80})
            {
                CHECK(tree.insert(value));
            }

            CHECK(tree.erase(50));
            CHECK(!tree.contains(50));
            CHECK(tree.size() == 6u);
            const std::vector<int> expected{20, 30, 40, 60, 70, 80};
            CHECK(tree.to_vector() == expected);
            CHECK(tree.validate());
        }

        // 删除只有左孩子的根。
        {
            BinarySearchTree<int> tree;
            for (int value : {50, 30, 20})
            {
                CHECK(tree.insert(value));
            }

            CHECK(tree.erase(50));
            CHECK(tree.size() == 2u);
            const std::vector<int> expected{20, 30};
            CHECK(tree.to_vector() == expected);
            CHECK(tree.validate());
        }

        // 删除唯一的根节点后回到空树。
        {
            BinarySearchTree<int> tree;
            CHECK(tree.insert(1));
            CHECK(tree.erase(1));
            CHECK(tree.empty());
            CHECK(tree.size() == 0u);
            CHECK(tree.height() == 0);
            CHECK(tree.validate());
        }

        // 删除不存在的元素。
        {
            BinarySearchTree<int> tree;
            for (int value : {50, 30, 70})
            {
                CHECK(tree.insert(value));
            }

            CHECK(!tree.erase(45));
            CHECK(!tree.erase(999));
            CHECK(tree.size() == 3u);
            CHECK(tree.validate());
        }

        // 连续删除直到空树，过程中始终保持有序。
        {
            BinarySearchTree<int> tree;
            for (int value : {50, 30, 70, 20, 40, 60, 80})
            {
                CHECK(tree.insert(value));
            }

            for (int value : {20, 40, 60, 80, 30, 70, 50})
            {
                CHECK(tree.erase(value));
                CHECK(tree.validate());
            }

            CHECK(tree.empty());
            CHECK(tree.size() == 0u);
            CHECK(!tree.erase(50));
        }

        return true;
    }

    bool test_bounds()
    {
        BinarySearchTree<int> tree;
        for (int value : {50, 30, 70, 20, 40, 60, 80})
        {
            CHECK(tree.insert(value));
        }

        const BinarySearchTree<int>& view = tree;

        // find 命中与未命中。
        const int* found = view.find(40);
        CHECK(found != nullptr);
        CHECK(*found == 40);
        CHECK(view.find(41) == nullptr);

        // find 与 floor/ceil 精确命中时应指向同一个节点内部值。
        CHECK(found == view.floor(40));
        CHECK(found == view.ceil(40));

        // min / max 边界。
        const int* minimum = view.min();
        CHECK(minimum != nullptr);
        CHECK(*minimum == 20);
        const int* maximum = view.max();
        CHECK(maximum != nullptr);
        CHECK(*maximum == 80);

        // floor：小于最小、等于元素、落在空隙、等于最大、大于最大。
        CHECK(view.floor(10) == nullptr);
        const int* lower = view.floor(20);
        CHECK(lower != nullptr && *lower == 20);
        lower = view.floor(25);
        CHECK(lower != nullptr && *lower == 20);
        lower = view.floor(80);
        CHECK(lower != nullptr && *lower == 80);
        lower = view.floor(90);
        CHECK(lower != nullptr && *lower == 80);

        // ceil：小于最小、等于元素、落在空隙、等于最大、大于最大。
        const int* upper = view.ceil(10);
        CHECK(upper != nullptr && *upper == 20);
        upper = view.ceil(20);
        CHECK(upper != nullptr && *upper == 20);
        upper = view.ceil(25);
        CHECK(upper != nullptr && *upper == 30);
        upper = view.ceil(80);
        CHECK(upper != nullptr && *upper == 80);
        CHECK(view.ceil(90) == nullptr);

        // 单节点树的各接口边界。
        BinarySearchTree<int> single;
        CHECK(single.insert(7));
        CHECK(single.floor(0) == nullptr);
        CHECK(single.min() != nullptr && *single.min() == 7);
        CHECK(single.max() != nullptr && *single.max() == 7);
        CHECK(single.floor(7) != nullptr && *single.floor(7) == 7);
        CHECK(single.ceil(7) != nullptr && *single.ceil(7) == 7);
        CHECK(single.floor(6) == nullptr);
        CHECK(single.ceil(8) == nullptr);
        CHECK(single.validate());
        return true;
    }

    bool test_to_vector()
    {
        const std::vector<int> input{42, 7, 99, 13, 64, 1, 55, 28, 76, 7, 42};
        BinarySearchTree<int> tree;
        std::set<int> reference;
        for (int value : input)
        {
            CHECK(tree.insert(value) == reference.insert(value).second);
        }

        const std::vector<int> expected(reference.begin(), reference.end());
        CHECK(tree.to_vector() == expected);
        CHECK(tree.size() == reference.size());
        CHECK(tree.validate());

        tree.clear();
        CHECK(tree.to_vector().empty());
        CHECK(tree.validate());

        // 清空后仍可继续使用。
        CHECK(tree.insert(5));
        const std::vector<int> after_clear{5};
        CHECK(tree.to_vector() == after_clear);
        return true;
    }

    bool test_height()
    {
        BinarySearchTree<int> tree;
        CHECK(tree.height() == 0);
        CHECK(tree.insert(10));
        CHECK(tree.height() == 1);

        for (int value : {5, 15, 3, 7, 12, 18})
        {
            CHECK(tree.insert(value));
        }
        CHECK(tree.height() == 3);
        CHECK(tree.validate());

        // 顺序插入 1..64，退化成右链，高度应为 64。
        BinarySearchTree<int> chain;
        std::set<int> reference;
        for (int value = 1; value <= 64; ++value)
        {
            CHECK(chain.insert(value));
            CHECK(reference.insert(value).second);
        }
        CHECK(chain.height() == 64);
        CHECK(chain.size() == 64u);
        CHECK(chain.validate());

        // 删除所有偶数，链上只剩奇数，高度应为 32，且与 std::set 一致。
        for (int value = 2; value <= 64; value += 2)
        {
            CHECK(chain.erase(value));
            CHECK(reference.erase(value) == 1u);
        }
        const std::vector<int> expected(reference.begin(), reference.end());
        CHECK(chain.to_vector() == expected);
        CHECK(chain.height() == 32);
        CHECK(chain.size() == 32u);
        CHECK(chain.validate());

        // 再逐个删空，验证退化链上的删除与清空。
        for (int value = 1; value <= 64; value += 2)
        {
            CHECK(chain.erase(value));
        }
        CHECK(chain.empty());
        CHECK(chain.height() == 0);
        CHECK(chain.size() == 0u);
        CHECK(chain.validate());

        // 退化链上的 clear()：迭代析构路径也要能一次清空。
        BinarySearchTree<int> chain_to_clear;
        for (int value = 1; value <= 64; ++value)
        {
            CHECK(chain_to_clear.insert(value));
        }
        chain_to_clear.clear();
        CHECK(chain_to_clear.empty());
        CHECK(chain_to_clear.size() == 0u);
        CHECK(chain_to_clear.height() == 0);
        CHECK(chain_to_clear.to_vector().empty());
        CHECK(chain_to_clear.validate());
        return true;
    }

    bool test_custom_compare()
    {
        BinarySearchTree<int, std::greater<int>> tree;
        for (int value : {50, 30, 70, 20, 40})
        {
            CHECK(tree.insert(value));
        }
        CHECK(!tree.insert(30));
        CHECK(tree.size() == 5u);

        // 比较器为 greater，中序结果应为降序。
        const std::vector<int> expected{70, 50, 40, 30, 20};
        CHECK(tree.to_vector() == expected);
        CHECK(tree.validate());

        // 比较器定义序下的最小/最大，分别对应数值最大/最小。
        const int* minimum = tree.min();
        CHECK(minimum != nullptr && *minimum == 70);
        const int* maximum = tree.max();
        CHECK(maximum != nullptr && *maximum == 20);

        CHECK(tree.erase(40));
        CHECK(!tree.contains(40));
        CHECK(tree.validate());
        return true;
    }

    bool test_random_against_std_set()
    {
        std::mt19937 generator(12345u);
        std::uniform_int_distribution<int> operation(0, 99);
        std::uniform_int_distribution<int> value_distribution(1, 1000);

        BinarySearchTree<int> tree;
        std::set<int> reference;

        for (int step = 0; step < 2000; ++step)
        {
            const int op = operation(generator);
            const int value = value_distribution(generator);

            if (op < 50)
            {
                // 50% 插入。
                const bool inserted = tree.insert(value);
                const bool reference_inserted = reference.insert(value).second;
                CHECK(inserted == reference_inserted);
            }
            else if (op < 90)
            {
                // 40% 删除。
                const bool erased = tree.erase(value);
                const bool reference_erased = reference.erase(value) == 1u;
                CHECK(erased == reference_erased);
            }
            else
            {
                // 10% 查询，顺带核对 find/floor/ceil。
                const int* found = tree.find(value);
                if (reference.find(value) != reference.end())
                {
                    CHECK(found != nullptr);
                    CHECK(*found == value);
                }
                else
                {
                    CHECK(found == nullptr);
                }

                const auto ceil_it = reference.lower_bound(value);
                const int* ceil_value = tree.ceil(value);
                if (ceil_it == reference.end())
                {
                    CHECK(ceil_value == nullptr);
                }
                else
                {
                    CHECK(ceil_value != nullptr && *ceil_value == *ceil_it);
                }

                auto floor_it = reference.upper_bound(value);
                const int* floor_value = tree.floor(value);
                if (floor_it == reference.begin())
                {
                    CHECK(floor_value == nullptr);
                }
                else
                {
                    --floor_it;
                    CHECK(floor_value != nullptr && *floor_value == *floor_it);
                }
            }

            // 每步都与 std::set 对照 size / contains / 中序序列，并做结构自检。
            CHECK(tree.contains(value) == (reference.find(value) != reference.end()));
            CHECK(tree.size() == reference.size());
            CHECK(tree.validate());

            const std::vector<int> actual = tree.to_vector();
            const std::vector<int> expected(reference.begin(), reference.end());
            CHECK(actual == expected);
        }

        return true;
    }
}

int main()
{
    if (!test_empty_tree()
        || !test_insert_and_duplicates()
        || !test_erase_shapes()
        || !test_bounds()
        || !test_to_vector()
        || !test_height()
        || !test_custom_compare()
        || !test_random_against_std_set())
    {
        return 1;
    }

    std::cout << "PASS: binary_search_tree\n";
    return 0;
}
