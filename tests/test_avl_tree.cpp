#include "../structure/tree/AVLTree.h"

#include <cmath>
#include <initializer_list>
#include <iostream>
#include <random>
#include <set>
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

using coordination::tree::AVLTree;

namespace
{
    std::vector<int> sorted_values(const std::set<int>& values)
    {
        return std::vector<int>(values.begin(), values.end());
    }

    void fill_tree(AVLTree<int>& tree, std::initializer_list<int> values)
    {
        for (int value : values)
        {
            tree.insert(value);
        }
    }
}

int main()
{
    // 空树的基本状态与查询。
    {
        AVLTree<int> tree;
        CHECK(tree.empty());
        CHECK(tree.size() == 0);
        CHECK(tree.height() == 0);
        CHECK(tree.validate());
        CHECK(tree.find(42) == nullptr);
        CHECK(tree.min() == nullptr);
        CHECK(tree.max() == nullptr);
        CHECK(tree.floor(42) == nullptr);
        CHECK(tree.ceil(42) == nullptr);
        CHECK(!tree.contains(42));
        CHECK(!tree.erase(42));
        CHECK(tree.to_vector().empty());
    }

    // 插入、重复插入、查找与高度。
    {
        AVLTree<int> tree;
        CHECK(tree.insert(50));
        CHECK(tree.insert(30));
        CHECK(tree.insert(70));
        CHECK(!tree.insert(50));
        CHECK(tree.size() == 3);
        CHECK(tree.height() == 2);
        CHECK(tree.validate());

        const int* found = tree.find(30);
        CHECK(found != nullptr);
        CHECK(*found == 30);
        CHECK(tree.find(31) == nullptr);
        CHECK(tree.contains(70));
        CHECK(!tree.contains(71));

        CHECK(tree.insert(20));
        CHECK(tree.insert(40));
        CHECK(tree.insert(60));
        CHECK(tree.insert(80));
        CHECK(tree.size() == 7);
        CHECK(tree.height() == 3);
        CHECK(tree.validate());
        CHECK(tree.to_vector() == std::vector<int>({20, 30, 40, 50, 60, 70, 80}));
    }

    // min/max/find/floor/ceil 的边界与中间值。
    {
        AVLTree<int> tree;
        fill_tree(tree, {20, 30, 40, 50, 60, 70, 80});

        const int* min_value = tree.min();
        const int* max_value = tree.max();
        CHECK(min_value != nullptr);
        CHECK(max_value != nullptr);
        CHECK(*min_value == 20);
        CHECK(*max_value == 80);

        const int* exact = tree.find(40);
        CHECK(exact != nullptr);
        CHECK(*exact == 40);
        CHECK(tree.find(45) == nullptr);

        CHECK(tree.floor(10) == nullptr);
        const int* floor_min = tree.floor(20);
        CHECK(floor_min != nullptr);
        CHECK(*floor_min == 20);
        const int* floor_gap = tree.floor(25);
        CHECK(floor_gap != nullptr);
        CHECK(*floor_gap == 20);
        const int* floor_exact = tree.floor(50);
        CHECK(floor_exact != nullptr);
        CHECK(*floor_exact == 50);
        const int* floor_high = tree.floor(90);
        CHECK(floor_high != nullptr);
        CHECK(*floor_high == 80);

        const int* ceil_low = tree.ceil(10);
        CHECK(ceil_low != nullptr);
        CHECK(*ceil_low == 20);
        const int* ceil_min = tree.ceil(20);
        CHECK(ceil_min != nullptr);
        CHECK(*ceil_min == 20);
        const int* ceil_gap = tree.ceil(25);
        CHECK(ceil_gap != nullptr);
        CHECK(*ceil_gap == 30);
        const int* ceil_exact = tree.ceil(50);
        CHECK(ceil_exact != nullptr);
        CHECK(*ceil_exact == 50);
        CHECK(tree.ceil(90) == nullptr);
    }

    // 删除叶子与只有一个孩子的节点。
    {
        AVLTree<int> tree;
        fill_tree(tree, {50, 30, 70, 20, 40, 60, 80});
        std::set<int> expected = {20, 30, 40, 50, 60, 70, 80};

        CHECK(tree.erase(20));
        expected.erase(20);
        CHECK(!tree.contains(20));
        CHECK(tree.to_vector() == sorted_values(expected));
        CHECK(tree.validate());

        CHECK(tree.erase(30));
        expected.erase(30);
        CHECK(!tree.contains(30));
        CHECK(tree.to_vector() == sorted_values(expected));
        CHECK(tree.validate());

        CHECK(!tree.erase(999));
        CHECK(tree.size() == expected.size());
        CHECK(tree.validate());
    }

    // 删除非根、有两个孩子的节点。
    {
        AVLTree<int> tree;
        fill_tree(tree, {50, 30, 70, 20, 40, 60, 80});
        std::set<int> expected = {20, 30, 40, 50, 60, 70, 80};

        CHECK(tree.erase(70));
        expected.erase(70);
        CHECK(!tree.contains(70));
        CHECK(tree.to_vector() == sorted_values(expected));
        CHECK(tree.validate());

        const int* min_value = tree.min();
        const int* max_value = tree.max();
        CHECK(min_value != nullptr);
        CHECK(max_value != nullptr);
        CHECK(*min_value == 20);
        CHECK(*max_value == 80);
    }

    // 删除根：后继是右孩子，且后继带右孩子。
    {
        AVLTree<int> tree;
        fill_tree(tree, {50, 30, 70, 20, 40, 80});
        std::set<int> expected = {20, 30, 40, 50, 70, 80};

        CHECK(tree.erase(50));
        expected.erase(50);
        CHECK(tree.size() == 5);
        CHECK(tree.to_vector() == sorted_values(expected));
        CHECK(tree.validate());

        const int* root_value = tree.min();
        CHECK(root_value != nullptr);
        CHECK(*root_value == 20);
    }

    // 删除根：后继不是右孩子，且后继本身带右孩子，需要先把它摘下来。
    {
        AVLTree<int> tree;
        fill_tree(tree, {50, 30, 70, 20, 40, 60, 80, 65});
        std::set<int> expected = {20, 30, 40, 50, 60, 65, 70, 80};

        CHECK(tree.erase(50));
        expected.erase(50);
        CHECK(!tree.contains(50));
        CHECK(tree.to_vector() == sorted_values(expected));
        CHECK(tree.validate());

        const int* min_value = tree.min();
        const int* max_value = tree.max();
        CHECK(min_value != nullptr);
        CHECK(max_value != nullptr);
        CHECK(*min_value == 20);
        CHECK(*max_value == 80);
    }

    // 删除单节点根与只有一个孩子的根。
    {
        AVLTree<int> tree;
        CHECK(tree.insert(7));
        CHECK(tree.height() == 1);
        CHECK(tree.validate());
        CHECK(tree.erase(7));
        CHECK(tree.empty());
        CHECK(tree.size() == 0);
        CHECK(tree.height() == 0);
        CHECK(tree.validate());
        CHECK(tree.min() == nullptr);
        CHECK(tree.max() == nullptr);
    }

    {
        AVLTree<int> tree;
        fill_tree(tree, {10, 5});
        CHECK(tree.height() == 2);
        CHECK(tree.validate());
        CHECK(tree.erase(10));
        CHECK(tree.size() == 1);
        CHECK(tree.height() == 1);
        CHECK(tree.validate());

        const int* remaining = tree.find(5);
        CHECK(remaining != nullptr);
        CHECK(*remaining == 5);
    }

    // clear 后可以继续使用。
    {
        AVLTree<int> tree;
        fill_tree(tree, {1, 2, 3, 4, 5});
        tree.clear();
        CHECK(tree.empty());
        CHECK(tree.size() == 0);
        CHECK(tree.height() == 0);
        CHECK(tree.validate());
        CHECK(tree.to_vector().empty());

        CHECK(tree.insert(9));
        CHECK(tree.insert(8));
        CHECK(tree.validate());
        CHECK(tree.to_vector() == std::vector<int>({8, 9}));
    }

    // 顺序插入 0..255，AVL 高度必须为对数级，再逐个删除到空。
    {
        AVLTree<int> tree;
        std::vector<int> increasing;
        increasing.reserve(256);

        for (int value = 0; value < 256; ++value)
        {
            CHECK(tree.insert(value));
            increasing.push_back(value);
        }

        CHECK(tree.size() == 256);
        CHECK(tree.height() <= 12);
        CHECK(tree.validate());
        CHECK(tree.to_vector() == increasing);

        for (int value = 0; value < 256; ++value)
        {
            CHECK(tree.erase(value));
        }

        CHECK(tree.empty());
        CHECK(tree.size() == 0);
        CHECK(tree.height() == 0);
        CHECK(tree.validate());
        CHECK(tree.find(0) == nullptr);
        CHECK(tree.min() == nullptr);
        CHECK(tree.max() == nullptr);
    }

    // 固定种子随机差分测试：每一步与 std::set<int> 完全对照，并校验整棵树。
    {
        AVLTree<int> tree;
        std::set<int> reference;
        std::mt19937 generator(2024);
        std::uniform_int_distribution<int> value_distribution(-500, 500);
        std::uniform_int_distribution<int> operation_distribution(0, 3);

        for (int step = 0; step < 5000; ++step)
        {
            const int value = value_distribution(generator);
            switch (operation_distribution(generator))
            {
            case 0:
            {
                const bool expected_inserted = reference.insert(value).second;
                CHECK(tree.insert(value) == expected_inserted);
                break;
            }
            case 1:
            {
                const bool expected_erased = reference.erase(value) != 0;
                CHECK(tree.erase(value) == expected_erased);
                break;
            }
            case 2:
            {
                const bool expected_contains = reference.find(value) != reference.end();
                CHECK(tree.contains(value) == expected_contains);
                break;
            }
            default:
            {
                CHECK(tree.to_vector() == sorted_values(reference));
                break;
            }
            }

            CHECK(tree.size() == reference.size());
            CHECK(tree.validate());
            CHECK(tree.to_vector() == sorted_values(reference));

            if (step % 50 == 0)
            {
                const double node_count = static_cast<double>(tree.size());
                const double height_limit = 2.0 * std::log2(node_count + 2.0) + 2.0;
                CHECK(static_cast<double>(tree.height()) <= height_limit);
            }

            if (step % 25 == 0)
            {
                const int probe = value_distribution(generator);

                const std::set<int>::const_iterator lower = reference.lower_bound(probe);
                const int* actual_ceil = tree.ceil(probe);
                if (lower == reference.end())
                {
                    CHECK(actual_ceil == nullptr);
                }
                else
                {
                    CHECK(actual_ceil != nullptr);
                    CHECK(*actual_ceil == *lower);
                }

                const std::set<int>::const_iterator upper = reference.upper_bound(probe);
                const int* actual_floor = tree.floor(probe);
                if (upper == reference.begin())
                {
                    CHECK(actual_floor == nullptr);
                }
                else
                {
                    std::set<int>::const_iterator floor_position = upper;
                    --floor_position;
                    CHECK(actual_floor != nullptr);
                    CHECK(*actual_floor == *floor_position);
                }

                const int* found = tree.find(probe);
                const bool expected_found = reference.find(probe) != reference.end();
                CHECK((found != nullptr) == expected_found);
                if (found != nullptr)
                {
                    CHECK(*found == probe);
                }

                if (!reference.empty())
                {
                    const int* actual_min = tree.min();
                    const int* actual_max = tree.max();
                    CHECK(actual_min != nullptr);
                    CHECK(actual_max != nullptr);
                    CHECK(*actual_min == *reference.begin());
                    CHECK(*actual_max == *reference.rbegin());
                }
            }
        }

        CHECK(tree.empty() == reference.empty());
        CHECK(tree.size() == reference.size());
        CHECK(tree.validate());
        CHECK(tree.to_vector() == sorted_values(reference));
    }

    std::cout << "PASS: avl_tree" << std::endl;
    return 0;
}
