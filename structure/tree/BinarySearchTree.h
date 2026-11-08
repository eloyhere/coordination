#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "../node/BinaryNode.h"

namespace coordination::tree
{
    // 普通二叉搜索树（不做旋转，也不维护平衡）。
    //
    // 设计要点：
    //  - 节点复用 node::BinaryNode<T>，由本容器负责 new/delete，不使用智能指针；
    //  - 等价判定统一为 !comp(a, b) && !comp(b, a)，因此重复插入返回 false；
    //  - find / floor / ceil 返回节点内部值的地址，容器修改后指针失效；
    //  - 删除有两个孩子的节点时，用中序后继（右子树最小节点）的值覆盖，
    //    再摘掉后继节点，这样只需要在原地调整指针。
    //
    // 复杂度：平均 O(log n)，最坏（有序插入退化成链）O(n)。
    template <typename T, typename Compare = std::less<T>>
    class BinarySearchTree
    {
    public:
        using value_type = T;
        using size_type = std::size_t;
        using key_compare = Compare;

        BinarySearchTree() = default;

        explicit BinarySearchTree(Compare comp)
            : comp_(std::move(comp))
        {
        }

        BinarySearchTree(const BinarySearchTree&) = delete;
        BinarySearchTree& operator=(const BinarySearchTree&) = delete;

        ~BinarySearchTree()
        {
            clear();
        }

        // 树是否为空。
        bool empty() const noexcept
        {
            return root_ == nullptr;
        }

        // 当前元素个数。
        size_type size() const noexcept
        {
            return size_;
        }

        // 释放全部节点。
        // 用「左孩子转到右侧」的迭代方式把树拉成一条右链再逐个删除，
        // 避免退化树在递归析构时爆栈，同时保证本函数不分配内存。
        void clear() noexcept
        {
            node_type* current = root_;
            while (current != nullptr)
            {
                if (current->left != nullptr)
                {
                    node_type* left = current->left;
                    current->left = left->right;
                    left->right = current;
                    current = left;
                }
                else
                {
                    node_type* right = current->right;
                    delete current;
                    current = right;
                }
            }

            root_ = nullptr;
            size_ = 0;
        }

        // 插入 value；已存在等价元素时不插入并返回 false。
        bool insert(const T& value)
        {
            node_type** link = &root_;
            while (*link != nullptr)
            {
                node_type* current = *link;
                if (comp_(value, current->value))
                {
                    link = &current->left;
                }
                else if (comp_(current->value, value))
                {
                    link = &current->right;
                }
                else
                {
                    return false;
                }
            }

            *link = new node_type(value);
            ++size_;
            return true;
        }

        // 删除与 value 等价的元素；不存在时返回 false。
        bool erase(const T& value)
        {
            node_type** link = &root_;
            node_type* target = nullptr;
            while (*link != nullptr)
            {
                if (comp_(value, (*link)->value))
                {
                    link = &(*link)->left;
                }
                else if (comp_((*link)->value, value))
                {
                    link = &(*link)->right;
                }
                else
                {
                    target = *link;
                    break;
                }
            }

            if (target == nullptr)
            {
                return false;
            }

            if (target->left != nullptr && target->right != nullptr)
            {
                // 两个孩子：找到中序后继，用后继的值覆盖待删值，再删除后继节点。
                node_type** successor_link = &target->right;
                while ((*successor_link)->left != nullptr)
                {
                    successor_link = &(*successor_link)->left;
                }

                node_type* successor = *successor_link;
                target->value = successor->value;
                *successor_link = successor->right;
                delete successor;
            }
            else
            {
                // 至多一个孩子：直接把唯一的孩子（或空指针）接到父链接上。
                node_type* child = target->left != nullptr ? target->left : target->right;
                *link = child;
                delete target;
            }

            --size_;
            return true;
        }

        bool contains(const T& value) const
        {
            return find_node(value) != nullptr;
        }

        const T* find(const T& value) const
        {
            const node_type* node = find_node(value);
            return node != nullptr ? &node->value : nullptr;
        }

        const T* min() const
        {
            const node_type* current = root_;
            if (current == nullptr)
            {
                return nullptr;
            }

            while (current->left != nullptr)
            {
                current = current->left;
            }

            return &current->value;
        }

        const T* max() const
        {
            const node_type* current = root_;
            if (current == nullptr)
            {
                return nullptr;
            }

            while (current->right != nullptr)
            {
                current = current->right;
            }

            return &current->value;
        }

        // 最大的 <= value 的元素；不存在时返回 nullptr。
        const T* floor(const T& value) const
        {
            const node_type* current = root_;
            const node_type* best = nullptr;
            while (current != nullptr)
            {
                if (comp_(value, current->value))
                {
                    // value < current：current 超过上界，只能往左找更小的候选。
                    current = current->left;
                }
                else
                {
                    // current <= value：可以先记下答案（相等时即精确命中），
                    // 再往右尝试更大的候选。
                    best = current;
                    current = current->right;
                }
            }

            return best != nullptr ? &best->value : nullptr;
        }

        // 最小的 >= value 的元素；不存在时返回 nullptr。
        const T* ceil(const T& value) const
        {
            const node_type* current = root_;
            const node_type* best = nullptr;
            while (current != nullptr)
            {
                if (comp_(current->value, value))
                {
                    // current < value：太小，只能往右找更大的候选。
                    current = current->right;
                }
                else
                {
                    // current >= value：可以先记下答案（相等时即精确命中），
                    // 再往左尝试更小的候选。
                    best = current;
                    current = current->left;
                }
            }

            return best != nullptr ? &best->value : nullptr;
        }

        // 中序遍历结果，按比较器定义的顺序升序排列。
        // 用显式栈代替递归，退化树也不会爆栈。
        std::vector<T> to_vector() const
        {
            std::vector<T> values;
            values.reserve(size_);

            std::vector<const node_type*> stack;
            const node_type* current = root_;
            while (current != nullptr || !stack.empty())
            {
                while (current != nullptr)
                {
                    stack.push_back(current);
                    current = current->left;
                }

                current = stack.back();
                stack.pop_back();
                values.push_back(current->value);
                current = current->right;
            }

            return values;
        }

        // 树高：空树 0，单节点 1。
        // 同样用显式栈逐层下探，避免退化树递归过深。
        int height() const
        {
            if (root_ == nullptr)
            {
                return 0;
            }

            struct Entry
            {
                const node_type* node;
                int depth;
            };

            int result = 0;
            std::vector<Entry> stack;
            stack.push_back({root_, 1});
            while (!stack.empty())
            {
                const Entry entry = stack.back();
                stack.pop_back();

                if (entry.depth > result)
                {
                    result = entry.depth;
                }

                if (entry.node->left != nullptr)
                {
                    stack.push_back({entry.node->left, entry.depth + 1});
                }
                if (entry.node->right != nullptr)
                {
                    stack.push_back({entry.node->right, entry.depth + 1});
                }
            }

            return result;
        }

        // 结构/顺序自检：
        //  - 每个节点都必须落在祖先给出的开区间 (lower, upper) 内，即 BST 有序；
        //  - 不允许自指或左右孩子指向同一个节点；
        //  - 实际节点数必须与 size_ 一致，环或共享子树都会导致计数超限。
        bool validate() const
        {
            struct Entry
            {
                const node_type* node;
                const node_type* lower; // 开区间下界，nullptr 表示无下界
                const node_type* upper; // 开区间上界，nullptr 表示无上界
            };

            std::size_t count = 0;
            std::vector<Entry> stack;
            if (root_ != nullptr)
            {
                stack.push_back({root_, nullptr, nullptr});
            }

            while (!stack.empty())
            {
                const Entry entry = stack.back();
                stack.pop_back();
                const node_type* current = entry.node;

                if (current->left == current || current->right == current)
                {
                    return false;
                }
                if (current->left != nullptr && current->left == current->right)
                {
                    return false;
                }
                if (entry.lower != nullptr && !comp_(entry.lower->value, current->value))
                {
                    return false;
                }
                if (entry.upper != nullptr && !comp_(current->value, entry.upper->value))
                {
                    return false;
                }

                ++count;
                if (count > size_)
                {
                    // 节点数超过记录值，说明存在环或重复共享的子树。
                    return false;
                }

                if (current->left != nullptr)
                {
                    stack.push_back({current->left, entry.lower, current});
                }
                if (current->right != nullptr)
                {
                    stack.push_back({current->right, current, entry.upper});
                }
            }

            return count == size_;
        }

    private:
        using node_type = node::BinaryNode<T>;

        // 查找与 value 等价的节点；未命中返回 nullptr。
        const node_type* find_node(const T& value) const
        {
            const node_type* current = root_;
            while (current != nullptr)
            {
                if (comp_(value, current->value))
                {
                    current = current->left;
                }
                else if (comp_(current->value, value))
                {
                    current = current->right;
                }
                else
                {
                    return current;
                }
            }

            return nullptr;
        }

        node_type* root_ = nullptr;
        size_type size_ = 0;
        Compare comp_{};
    };
}
