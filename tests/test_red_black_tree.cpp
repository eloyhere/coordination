#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <random>
#include <set>
#include <vector>

#include "structure/tree/RedBlackTree.h"

namespace
{
    using coordination::tree::RedBlackTree;

    // 断言：条件不成立时打印 "FAIL: <原因>"，并让当前用例提前返回。
#define CHECK(condition, reason) \
    do \
    { \
        if (!(condition)) \
        { \
            std::printf("FAIL: %s\n", (reason)); \
            return false; \
        } \
    } while (false)

    // 带步骤编号的断言，随机测试与循环测试用。
    bool check_step(bool condition, int step, const char* reason)
    {
        if (!condition)
        {
            std::printf("FAIL: 第 %d 步：%s\n", step, reason);
        }
        return condition;
    }

    bool check_sequence(const std::vector<int>& actual, const std::vector<int>& expected, int step, const char* reason)
    {
        if (actual == expected)
        {
            return true;
        }
        std::printf("FAIL: 第 %d 步：%s（实际 %d 项，期望 %d 项）\n", step, reason,
                    static_cast<int>(actual.size()), static_cast<int>(expected.size()));
        return false;
    }

    template <typename Compare>
    void insert_all(RedBlackTree<int, Compare>& tree, std::initializer_list<int> values)
    {
        for (int value : values)
        {
            tree.insert(value);
        }
    }

    bool test_empty_tree()
    {
        RedBlackTree<int> tree;
        CHECK(tree.empty(), "空树 empty() 应为 true");
        CHECK(tree.size() == 0, "空树 size() 应为 0");
        CHECK(tree.height() == 0, "空树 height() 应为 0");
        CHECK(tree.validate(), "空树 validate() 应为 true");
        CHECK(tree.find(1) == nullptr, "空树 find 应返回 nullptr");
        CHECK(tree.min() == nullptr, "空树 min 应返回 nullptr");
        CHECK(tree.max() == nullptr, "空树 max 应返回 nullptr");
        CHECK(tree.floor(1) == nullptr, "空树 floor 应返回 nullptr");
        CHECK(tree.ceil(1) == nullptr, "空树 ceil 应返回 nullptr");
        CHECK(tree.to_vector().empty(), "空树 to_vector 应为空");
        CHECK(!tree.contains(1), "空树 contains 应为 false");
        CHECK(!tree.erase(1), "空树 erase 应返回 false");
        return true;
    }

    bool test_insert_and_duplicate()
    {
        RedBlackTree<int> tree;
        CHECK(tree.insert(10), "首次插入 10 应返回 true");
        CHECK(tree.size() == 1, "插入 10 后 size 应为 1");
        CHECK(tree.height() == 1, "单节点树高度应为 1");
        CHECK(tree.validate(), "单节点树 validate() 应为 true");
        CHECK(tree.contains(10), "contains(10) 应为 true");
        CHECK(tree.find(10) != nullptr && *tree.find(10) == 10, "find(10) 应返回 10");

        CHECK(!tree.insert(10), "重复插入 10 应返回 false");
        CHECK(tree.size() == 1, "重复插入后 size 应保持 1");
        CHECK(tree.to_vector() == std::vector<int>({10}), "重复插入后仍应只有 10");
        CHECK(tree.validate(), "重复插入后 validate() 应为 true");

        const int values[] = {5, 15, 3, 7, 12, 18};
        for (int value : values)
        {
            CHECK(tree.insert(value), "插入新元素应返回 true");
        }
        CHECK(tree.size() == 7, "共应有 7 个元素");
        CHECK(tree.validate(), "多次插入后 validate() 应为 true");

        for (int value : values)
        {
            CHECK(!tree.insert(value), "重复插入已有元素应返回 false");
        }
        CHECK(tree.size() == 7, "重复插入后元素数不应变化");
        CHECK(tree.validate(), "重复插入后 validate() 应为 true");
        return true;
    }

    bool test_lookup()
    {
        RedBlackTree<int> tree;
        insert_all(tree, {10, 20, 30, 40, 50});
        CHECK(tree.validate(), "查找用例的树应满足不变量");

        CHECK(tree.min() != nullptr && *tree.min() == 10, "min 应为 10");
        CHECK(tree.max() != nullptr && *tree.max() == 50, "max 应为 50");
        CHECK(tree.find(30) != nullptr && *tree.find(30) == 30, "find(30) 应为 30");
        CHECK(tree.find(35) == nullptr, "find(35) 应为 nullptr");
        CHECK(tree.find(5) == nullptr, "find(5) 应为 nullptr");

        CHECK(tree.floor(25) != nullptr && *tree.floor(25) == 20, "floor(25) 应为 20");
        CHECK(tree.floor(30) != nullptr && *tree.floor(30) == 30, "floor(30) 应为 30");
        CHECK(tree.floor(9) == nullptr, "floor(9) 应为 nullptr");
        CHECK(tree.floor(10) != nullptr && *tree.floor(10) == 10, "floor(10) 应为 10");
        CHECK(tree.floor(50) != nullptr && *tree.floor(50) == 50, "floor(50) 应为 50");
        CHECK(tree.floor(100) != nullptr && *tree.floor(100) == 50, "floor(100) 应为 50");

        CHECK(tree.ceil(25) != nullptr && *tree.ceil(25) == 30, "ceil(25) 应为 30");
        CHECK(tree.ceil(30) != nullptr && *tree.ceil(30) == 30, "ceil(30) 应为 30");
        CHECK(tree.ceil(55) == nullptr, "ceil(55) 应为 nullptr");
        CHECK(tree.ceil(10) != nullptr && *tree.ceil(10) == 10, "ceil(10) 应为 10");
        CHECK(tree.ceil(5) != nullptr && *tree.ceil(5) == 10, "ceil(5) 应为 10");
        return true;
    }

    bool test_erase_leaf()
    {
        RedBlackTree<int> tree;
        insert_all(tree, {50, 30, 70, 20, 40, 60, 80});
        CHECK(tree.validate(), "删除叶子的树应满足不变量");

        CHECK(tree.erase(20), "删除叶子 20 应返回 true");
        CHECK(tree.size() == 6, "删除叶子后 size 应为 6");
        CHECK(!tree.contains(20), "删除后不应再包含 20");
        CHECK(tree.validate(), "删除叶子后 validate() 应为 true");

        CHECK(tree.erase(80), "删除叶子 80 应返回 true");
        CHECK(tree.validate(), "删除叶子 80 后 validate() 应为 true");
        CHECK(tree.to_vector() == std::vector<int>({30, 40, 50, 60, 70}), "删除两个叶子后中序应正确");

        CHECK(!tree.erase(999), "删除不存在的 999 应返回 false");
        CHECK(!tree.erase(20), "重复删除 20 应返回 false");
        CHECK(tree.size() == 5, "删除不存在元素后 size 不应变化");
        CHECK(tree.validate(), "删除不存在元素后 validate() 应为 true");
        return true;
    }

    bool test_erase_single_child()
    {
        RedBlackTree<int> tree;
        insert_all(tree, {10, 5, 15, 3});
        CHECK(tree.validate(), "单孩子用例的树应满足不变量");
        CHECK(tree.erase(5), "删除只有左孩子的 5 应返回 true");
        CHECK(tree.size() == 3, "删除后 size 应为 3");
        CHECK(tree.to_vector() == std::vector<int>({3, 10, 15}), "删除只有左孩子的节点后中序应正确");
        CHECK(tree.validate(), "删除只有左孩子的节点后 validate() 应为 true");

        RedBlackTree<int> right_tree;
        insert_all(right_tree, {10, 5, 15, 20});
        CHECK(right_tree.validate(), "单孩子用例的树应满足不变量");
        CHECK(right_tree.erase(15), "删除只有右孩子的 15 应返回 true");
        CHECK(right_tree.to_vector() == std::vector<int>({5, 10, 20}), "删除只有右孩子的节点后中序应正确");
        CHECK(right_tree.validate(), "删除只有右孩子的节点后 validate() 应为 true");
        return true;
    }

    bool test_erase_two_children()
    {
        // 后继位于右子树深处：删除 50，后继是 55。
        RedBlackTree<int> deep;
        insert_all(deep, {50, 30, 70, 60, 80, 55, 65});
        CHECK(deep.validate(), "双孩子用例的树应满足不变量");
        CHECK(deep.erase(50), "删除有两个孩子的根 50 应返回 true");
        CHECK(deep.size() == 6, "删除后 size 应为 6");
        CHECK(deep.to_vector() == std::vector<int>({30, 55, 60, 65, 70, 80}), "删除 50 后中序应正确");
        CHECK(deep.validate(), "删除 50 后 validate() 应为 true");

        // 后继就是右孩子：删除 20，后继是 30。
        RedBlackTree<int> direct;
        insert_all(direct, {10, 5, 20, 15, 30});
        CHECK(direct.validate(), "双孩子用例的树应满足不变量");
        CHECK(direct.erase(20), "删除有两个孩子的 20 应返回 true");
        CHECK(direct.to_vector() == std::vector<int>({5, 10, 15, 30}), "删除 20 后中序应正确");
        CHECK(direct.validate(), "删除 20 后 validate() 应为 true");

        // 根的后继是右孩子：删除根 10。
        CHECK(direct.erase(10), "删除有两个孩子的根 10 应返回 true");
        CHECK(direct.to_vector() == std::vector<int>({5, 15, 30}), "删除根 10 后中序应正确");
        CHECK(direct.validate(), "删除根 10 后 validate() 应为 true");
        return true;
    }

    bool test_erase_root()
    {
        RedBlackTree<int> single;
        single.insert(42);
        CHECK(single.erase(42), "删除唯一节点应返回 true");
        CHECK(single.empty(), "删除唯一节点后应为空");
        CHECK(single.size() == 0, "删除唯一节点后 size 应为 0");
        CHECK(single.height() == 0, "删除唯一节点后 height 应为 0");
        CHECK(single.validate(), "删除唯一节点后 validate() 应为 true");
        CHECK(!single.erase(42), "再次删除应返回 false");
        CHECK(single.insert(1), "清空后重新插入应成功");
        CHECK(single.validate(), "重新插入后 validate() 应为 true");
        return true;
    }

    bool test_to_vector_against_set()
    {
        RedBlackTree<int> tree;
        std::set<int> reference;
        const int inserted[] = {42, 7, 19, 3, 88, 56, 71, 7, 19, 100, 1, 64};

        for (int value : inserted)
        {
            tree.insert(value);
            reference.insert(value);
            if (!check_sequence(tree.to_vector(), std::vector<int>(reference.begin(), reference.end()), 0, "插入过程中 to_vector 应与 std::set 一致"))
            {
                return false;
            }
            if (!check_step(tree.validate(), 0, "插入过程中 validate() 应为 true"))
            {
                return false;
            }
        }

        const int removed[] = {42, 3, 100, 56};
        for (int value : removed)
        {
            tree.erase(value);
            reference.erase(value);
            if (!check_sequence(tree.to_vector(), std::vector<int>(reference.begin(), reference.end()), 0, "删除过程中 to_vector 应与 std::set 一致"))
            {
                return false;
            }
            if (!check_step(tree.validate(), 0, "删除过程中 validate() 应为 true"))
            {
                return false;
            }
        }
        return true;
    }

    bool test_clear()
    {
        RedBlackTree<int> tree;
        for (int value = 0; value < 100; ++value)
        {
            tree.insert(value);
        }
        CHECK(tree.size() == 100, "clear 前应有 100 个元素");
        CHECK(tree.validate(), "clear 前 validate() 应为 true");

        tree.clear();
        CHECK(tree.empty(), "clear 后 empty() 应为 true");
        CHECK(tree.size() == 0, "clear 后 size 应为 0");
        CHECK(tree.height() == 0, "clear 后 height 应为 0");
        CHECK(tree.validate(), "clear 后 validate() 应为 true");
        CHECK(tree.to_vector().empty(), "clear 后 to_vector 应为空");
        CHECK(tree.min() == nullptr && tree.max() == nullptr, "clear 后 min/max 应为 nullptr");

        CHECK(tree.insert(7), "clear 后重新插入应成功");
        CHECK(tree.size() == 1, "重新插入后 size 应为 1");
        CHECK(tree.validate(), "重新插入后 validate() 应为 true");
        return true;
    }

    bool test_height_extremes()
    {
        RedBlackTree<int> ascending;
        for (int value = 0; value < 256; ++value)
        {
            ascending.insert(value);
        }
        CHECK(ascending.size() == 256, "顺序插入 0..255 后 size 应为 256");
        CHECK(ascending.validate(), "顺序插入后 validate() 应为 true");
        CHECK(ascending.height() <= 16, "顺序插入 256 个元素后高度应不超过 16");
        CHECK(ascending.height() >= 8, "256 个元素的高度应至少为 8");

        const std::vector<int> ascending_values = ascending.to_vector();
        CHECK(ascending_values.size() == 256, "to_vector 应有 256 项");
        bool ordered = true;
        for (std::size_t i = 1; i < ascending_values.size(); ++i)
        {
            if (!(ascending_values[i - 1] < ascending_values[i]))
            {
                ordered = false;
            }
        }
        CHECK(ordered, "to_vector 应严格升序");

        for (int value = 0; value < 256; ++value)
        {
            CHECK(ascending.erase(value), "顺序删除应返回 true");
        }
        CHECK(ascending.empty(), "全部删除后 empty() 应为 true");
        CHECK(ascending.size() == 0, "全部删除后 size 应为 0");
        CHECK(ascending.height() == 0, "全部删除后 height 应为 0");
        CHECK(ascending.validate(), "全部删除后 validate() 应为 true");

        RedBlackTree<int> descending;
        for (int value = 255; value >= 0; --value)
        {
            descending.insert(value);
        }
        CHECK(descending.size() == 256, "逆序插入后 size 应为 256");
        CHECK(descending.validate(), "逆序插入后 validate() 应为 true");
        CHECK(descending.height() <= 16, "逆序插入 256 个元素后高度应不超过 16");
        CHECK(descending.height() >= 8, "逆序插入 256 个元素的高度应至少为 8");

        for (int value = 255; value >= 0; --value)
        {
            CHECK(descending.erase(value), "逆序删除应返回 true");
        }
        CHECK(descending.empty(), "逆序全部删除后 empty() 应为 true");
        CHECK(descending.size() == 0, "逆序全部删除后 size 应为 0");
        CHECK(descending.height() == 0, "逆序全部删除后 height 应为 0");
        CHECK(descending.validate(), "逆序全部删除后 validate() 应为 true");
        return true;
    }

    // 对每个规模、每个目标值分别建树并删除，覆盖不同树形下的修复分支。
    bool test_systematic_erase()
    {
        for (int count = 0; count <= 64; ++count)
        {
            std::vector<int> expected;
            for (int target = 0; target < count; ++target)
            {
                RedBlackTree<int> tree;
                for (int value = 0; value < count; ++value)
                {
                    tree.insert(value);
                }

                expected.clear();
                expected.reserve(static_cast<std::size_t>(count - 1));
                for (int value = 0; value < count; ++value)
                {
                    if (value != target)
                    {
                        expected.push_back(value);
                    }
                }

                if (!tree.erase(target))
                {
                    std::printf("FAIL: 规模 %d 删除 %d 应返回 true\n", count, target);
                    return false;
                }
                if (tree.to_vector() != expected)
                {
                    std::printf("FAIL: 规模 %d 删除 %d 后中序不符\n", count, target);
                    return false;
                }
                if (tree.size() != static_cast<std::size_t>(count - 1))
                {
                    std::printf("FAIL: 规模 %d 删除 %d 后 size 不符\n", count, target);
                    return false;
                }
                if (!tree.validate())
                {
                    std::printf("FAIL: 规模 %d 删除 %d 后 validate() 失败\n", count, target);
                    return false;
                }
            }
        }
        return true;
    }

    // 多轮乱序删除，检查删除修复的连续组合。
    bool test_erase_all_shuffled()
    {
        std::mt19937 engine(2026);
        for (int round = 0; round < 8; ++round)
        {
            RedBlackTree<int> tree;
            std::vector<int> order;
            order.reserve(256);
            for (int value = 0; value < 256; ++value)
            {
                tree.insert(value);
                order.push_back(value);
            }
            std::shuffle(order.begin(), order.end(), engine);

            for (int value : order)
            {
                if (!tree.erase(value))
                {
                    std::printf("FAIL: 第 %d 轮删除 %d 应返回 true\n", round, value);
                    return false;
                }
                if (!tree.validate())
                {
                    std::printf("FAIL: 第 %d 轮删除 %d 后 validate() 失败\n", round, value);
                    return false;
                }
            }

            CHECK(tree.empty(), "全部删除后 empty() 应为 true");
            CHECK(tree.size() == 0, "全部删除后 size 应为 0");
            CHECK(tree.height() == 0, "全部删除后 height 应为 0");
            CHECK(tree.validate(), "全部删除后 validate() 应为 true");
        }
        return true;
    }

    bool test_custom_compare()
    {
        RedBlackTree<int, std::greater<int>> tree(std::greater<int>{});
        insert_all(tree, {3, 1, 4, 1, 5, 9, 2, 6});
        CHECK(tree.size() == 7, "去重后应有 7 个元素");
        CHECK(tree.validate(), "greater 比较器下 validate() 应为 true");
        CHECK(tree.to_vector() == std::vector<int>({9, 6, 5, 4, 3, 2, 1}), "greater 比较器下中序应为降序");
        CHECK(tree.find(4) != nullptr && *tree.find(4) == 4, "greater 比较器下 find(4) 应为 4");
        CHECK(tree.min() != nullptr && *tree.min() == 9, "greater 比较器下 min 应为 9");
        CHECK(tree.max() != nullptr && *tree.max() == 1, "greater 比较器下 max 应为 1");
        CHECK(tree.erase(4), "greater 比较器下删除应成功");
        CHECK(tree.to_vector() == std::vector<int>({9, 6, 5, 3, 2, 1}), "删除后降序应正确");
        CHECK(tree.validate(), "greater 比较器下删除后 validate() 应为 true");
        return true;
    }

    bool test_random_differential()
    {
        std::mt19937 engine(777);
        std::uniform_int_distribution<int> value_distribution(0, 299);
        std::uniform_int_distribution<int> operation_distribution(0, 3);
        std::uniform_int_distribution<int> query_distribution(-50, 350);

        RedBlackTree<int> tree;
        std::set<int> reference;

        for (int step = 0; step < 5000; ++step)
        {
            const int value = value_distribution(engine);
            const int operation = operation_distribution(engine);

            switch (operation)
            {
            case 0:
            {
                const bool inserted = tree.insert(value);
                const bool reference_inserted = reference.insert(value).second;
                if (!check_step(inserted == reference_inserted, step, "insert 返回值应与 std::set 一致"))
                {
                    return false;
                }
                break;
            }
            case 1:
            {
                const bool erased = tree.erase(value);
                const bool reference_erased = reference.erase(value) != 0;
                if (!check_step(erased == reference_erased, step, "erase 返回值应与 std::set 一致"))
                {
                    return false;
                }
                break;
            }
            case 2:
            {
                const bool found = tree.contains(value);
                const bool reference_found = reference.find(value) != reference.end();
                if (!check_step(found == reference_found, step, "contains 应与 std::set 一致"))
                {
                    return false;
                }
                break;
            }
            default:
            {
                if (!check_sequence(tree.to_vector(), std::vector<int>(reference.begin(), reference.end()), step, "to_vector 应与 std::set 一致"))
                {
                    return false;
                }
                break;
            }
            }

            if (!check_step(tree.validate(), step, "validate() 应为 true"))
            {
                return false;
            }
            if (!check_step(tree.size() == reference.size(), step, "size() 应与 std::set 一致"))
            {
                return false;
            }

            // 每步顺带对照 min/max/floor/ceil。
            const int* tree_min = tree.min();
            const int* tree_max = tree.max();
            if (reference.empty())
            {
                if (!check_step(tree_min == nullptr && tree_max == nullptr, step, "空树 min/max 应为 nullptr"))
                {
                    return false;
                }
            }
            else
            {
                if (!check_step(tree_min != nullptr && *tree_min == *reference.begin(), step, "min 应为 std::set 首元素"))
                {
                    return false;
                }
                if (!check_step(tree_max != nullptr && *tree_max == *reference.rbegin(), step, "max 应为 std::set 末元素"))
                {
                    return false;
                }
            }

            const int query = query_distribution(engine);

            bool has_floor = false;
            int expected_floor = 0;
            std::set<int>::const_iterator floor_iterator = reference.upper_bound(query);
            if (floor_iterator != reference.begin())
            {
                --floor_iterator;
                has_floor = true;
                expected_floor = *floor_iterator;
            }

            const int* tree_floor = tree.floor(query);
            if (!check_step((tree_floor != nullptr) == has_floor, step, "floor 是否存在应与 std::set 对照一致"))
            {
                return false;
            }
            if (has_floor && !check_step(*tree_floor == expected_floor, step, "floor 的值应与 std::set 对照一致"))
            {
                return false;
            }

            const std::set<int>::const_iterator ceil_iterator = reference.lower_bound(query);
            const bool has_ceil = ceil_iterator != reference.end();
            const int* tree_ceil = tree.ceil(query);
            if (!check_step((tree_ceil != nullptr) == has_ceil, step, "ceil 是否存在应与 std::set 对照一致"))
            {
                return false;
            }
            if (has_ceil && !check_step(*tree_ceil == *ceil_iterator, step, "ceil 的值应与 std::set 对照一致"))
            {
                return false;
            }
        }

        if (!check_sequence(tree.to_vector(), std::vector<int>(reference.begin(), reference.end()), 5000, "随机测试结束后 to_vector 应与 std::set 一致"))
        {
            return false;
        }
        CHECK(tree.size() == reference.size(), "随机测试结束后 size 应一致");
        CHECK(tree.validate(), "随机测试结束后 validate() 应为 true");

        for (int value : reference)
        {
            tree.erase(value);
        }
        CHECK(tree.empty(), "随机测试结束后全部删除应为空");
        CHECK(tree.size() == 0, "随机测试结束后全部删除 size 应为 0");
        CHECK(tree.height() == 0, "随机测试结束后全部删除 height 应为 0");
        CHECK(tree.validate(), "随机测试结束后全部删除 validate() 应为 true");
        return true;
    }

    bool run_all_tests()
    {
        return test_empty_tree()
            && test_insert_and_duplicate()
            && test_lookup()
            && test_erase_leaf()
            && test_erase_single_child()
            && test_erase_two_children()
            && test_erase_root()
            && test_to_vector_against_set()
            && test_clear()
            && test_height_extremes()
            && test_systematic_erase()
            && test_erase_all_shuffled()
            && test_custom_compare()
            && test_random_differential();
    }
}

int main()
{
    if (!run_all_tests())
    {
        return 1;
    }

    std::printf("PASS: red_black_tree\n");
    return 0;
}
