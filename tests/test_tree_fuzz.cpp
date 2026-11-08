// 三棵有序树的差分模糊测试：
// 用 std::set 作为参照，随机插入/删除/查询，逐步校验 size、内容、有序性、
// floor/ceil、min/max 以及各树自己的结构不变量（validate）。
#include <cmath>
#include <cstdio>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "structure/tree/AVLTree.h"
#include "structure/tree/BinarySearchTree.h"
#include "structure/tree/RedBlackTree.h"

namespace
{
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

    template <typename Tree>
    void check_empty(const char* name)
    {
        Tree tree;

        expect(tree.empty(), std::string(name) + ": 新建树应为空");
        expect(tree.size() == 0, std::string(name) + ": 新建树 size 应为 0");
        expect(tree.height() == 0, std::string(name) + ": 空树高度应为 0");
        expect(tree.find(1) == nullptr, std::string(name) + ": 空树 find 应返回 nullptr");
        expect(tree.min() == nullptr, std::string(name) + ": 空树 min 应返回 nullptr");
        expect(tree.max() == nullptr, std::string(name) + ": 空树 max 应返回 nullptr");
        expect(tree.floor(5) == nullptr, std::string(name) + ": 空树 floor 应返回 nullptr");
        expect(tree.ceil(5) == nullptr, std::string(name) + ": 空树 ceil 应返回 nullptr");
        expect(tree.to_vector().empty(), std::string(name) + ": 空树 to_vector 应为空");
        expect(tree.validate(), std::string(name) + ": 空树 validate 应为 true");
        expect(!tree.erase(5), std::string(name) + ": 空树 erase 应返回 false");
        expect(!tree.contains(5), std::string(name) + ": 空树 contains 应返回 false");

        tree.clear();
        expect(tree.empty(), std::string(name) + ": 空树 clear 后仍为空");
    }

    template <typename Tree>
    void check_probe(const char* name, const Tree& tree, const std::set<int>& reference, int probe, int step)
    {
        const int* found = tree.find(probe);
        const bool expected_found = reference.find(probe) != reference.end();
        expect((found != nullptr) == expected_found, at_step(name, "find 命中与否不一致", step));
        if (found != nullptr && expected_found)
        {
            expect(*found == probe, at_step(name, "find 返回的值不对", step));
        }

        const int* floor_value = tree.floor(probe);
        const auto upper = reference.upper_bound(probe);
        if (upper == reference.begin())
        {
            expect(floor_value == nullptr, at_step(name, "floor 应不存在", step));
        }
        else
        {
            const int expected_floor = *std::prev(upper);
            expect(floor_value != nullptr && *floor_value == expected_floor, at_step(name, "floor 值不对", step));
        }

        const int* ceil_value = tree.ceil(probe);
        const auto lower = reference.lower_bound(probe);
        if (lower == reference.end())
        {
            expect(ceil_value == nullptr, at_step(name, "ceil 应不存在", step));
        }
        else
        {
            expect(ceil_value != nullptr && *ceil_value == *lower, at_step(name, "ceil 值不对", step));
        }

        const int* min_value = tree.min();
        const int* max_value = tree.max();
        expect((min_value == nullptr) == reference.empty(), at_step(name, "min 空状态不对", step));
        expect((max_value == nullptr) == reference.empty(), at_step(name, "max 空状态不对", step));
        if (min_value != nullptr)
        {
            expect(*min_value == *reference.begin(), at_step(name, "min 值不对", step));
        }
        if (max_value != nullptr)
        {
            expect(*max_value == *reference.rbegin(), at_step(name, "max 值不对", step));
        }
    }

    template <typename Tree>
    void run_random(const char* name, bool balanced)
    {
        Tree tree;
        std::set<int> reference;

        std::mt19937 rng(20241109u);
        std::uniform_int_distribution<int> value_dist(0, 300);
        std::uniform_int_distribution<int> op_dist(0, 99);

        for (int step = 0; step < 4000; ++step)
        {
            const int value = value_dist(rng);
            const int op = op_dist(rng);

            if (op < 45)
            {
                const bool expected = reference.insert(value).second;
                expect(tree.insert(value) == expected, at_step(name, "insert 返回值不一致", step));
            }
            else if (op < 80)
            {
                const bool expected = reference.erase(value) != 0;
                expect(tree.erase(value) == expected, at_step(name, "erase 返回值不一致", step));
            }
            else
            {
                const bool expected = reference.find(value) != reference.end();
                expect(tree.contains(value) == expected, at_step(name, "contains 不一致", step));
            }

            expect(tree.size() == reference.size(), at_step(name, "size 不一致", step));
            expect(tree.validate(), at_step(name, "validate 失败", step));

            if (step % 32 == 0)
            {
                const std::vector<int> actual = tree.to_vector();
                const std::vector<int> expected(reference.begin(), reference.end());
                expect(actual == expected, at_step(name, "中序序列不一致", step));

                if (balanced)
                {
                    const double bound = 2.0 * std::log2(static_cast<double>(reference.size()) + 2.0) + 2.0;
                    expect(static_cast<double>(tree.height()) <= bound, at_step(name, "平衡树高度超出上界", step));
                }
            }

            if (step % 8 == 0)
            {
                check_probe(name, tree, reference, value_dist(rng), step);
            }
        }

        // 清空后必须回到空树状态，顺带验证 clear 不泄漏、不悬空。
        tree.clear();
        expect(tree.empty(), std::string(name) + ": clear 后应为空");
        expect(tree.size() == 0, std::string(name) + ": clear 后 size 应为 0");
        expect(tree.height() == 0, std::string(name) + ": clear 后高度应为 0");
        expect(tree.validate(), std::string(name) + ": clear 后 validate 应为 true");
    }

    template <typename Tree>
    void check_sequential(const char* name, bool balanced)
    {
        Tree tree;
        std::set<int> reference;

        for (int value = 0; value < 256; ++value)
        {
            tree.insert(value);
            reference.insert(value);
        }
        expect(tree.size() == 256, std::string(name) + ": 顺序插入后 size 应为 256");
        expect(tree.validate(), std::string(name) + ": 顺序插入后 validate 失败");
        if (balanced)
        {
            // 只有平衡树能保证对数高度；普通 BST 顺序插入会退化成 256 层链。
            expect(tree.height() <= 16, std::string(name) + ": 顺序插入后高度超出上界");
        }

        for (int value = 0; value < 256; value += 2)
        {
            expect(tree.erase(value), std::string(name) + ": 删除偶数失败");
            reference.erase(value);
        }
        expect(tree.size() == reference.size(), std::string(name) + ": 删除一半后 size 不一致");
        expect(tree.validate(), std::string(name) + ": 删除一半后 validate 失败");

        const std::vector<int> actual = tree.to_vector();
        const std::vector<int> expected(reference.begin(), reference.end());
        expect(actual == expected, std::string(name) + ": 删除一半后中序序列不一致");
    }
}

int main()
{
    using coordination::tree::AVLTree;
    using coordination::tree::BinarySearchTree;
    using coordination::tree::RedBlackTree;

    check_empty<BinarySearchTree<int>>("bst");
    check_empty<AVLTree<int>>("avl");
    check_empty<RedBlackTree<int>>("rb");

    run_random<BinarySearchTree<int>>("bst", false);
    run_random<AVLTree<int>>("avl", true);
    run_random<RedBlackTree<int>>("rb", true);

    check_sequential<BinarySearchTree<int>>("bst", false);
    check_sequential<AVLTree<int>>("avl", true);
    check_sequential<RedBlackTree<int>>("rb", true);

    if (g_failures == 0)
    {
        std::printf("PASS: tree_fuzz\n");
        return 0;
    }

    std::printf("FAILURES: %d\n", g_failures);
    return 1;
}
