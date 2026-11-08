#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "../node/AVLNode.h"

namespace coordination::tree
{
    // AVL 平衡二叉搜索树：所有节点由容器 new/delete，结点保存父指针与子树高度。
    // 插入和删除沿 parent 指针自底向上 pull() 并旋转，保证任意节点平衡因子绝对值不超过 1。
    // 元素等价由 Compare 判定：!comp(a, b) && !comp(b, a)。
    template <typename T, typename Compare = std::less<T>>
    class AVLTree
    {
    public:
        using value_type = T;
        using size_type = std::size_t;
        using key_compare = Compare;

        AVLTree() = default;

        explicit AVLTree(Compare comp)
            : compare_(std::move(comp))
        {
        }

        AVLTree(const AVLTree&) = delete;
        AVLTree& operator=(const AVLTree&) = delete;

        ~AVLTree()
        {
            clear();
        }

        bool empty() const noexcept
        {
            return size_ == 0;
        }

        size_type size() const noexcept
        {
            return size_;
        }

        // 迭代删除全部节点，不依赖递归深度；高度使用后序方式沿父指针收缩。
        void clear() noexcept
        {
            node_type* current = root_;
            while (current != nullptr)
            {
                if (current->left != nullptr)
                {
                    current = current->left;
                    continue;
                }

                if (current->right != nullptr)
                {
                    current = current->right;
                    continue;
                }

                node_type* parent = current->parent;
                if (parent != nullptr)
                {
                    if (parent->left == current)
                    {
                        parent->left = nullptr;
                    }
                    else
                    {
                        parent->right = nullptr;
                    }
                }

                delete current;
                current = parent;
            }

            root_ = nullptr;
            size_ = 0;
        }

        // 已存在等价元素时返回 false，不覆盖原值。
        bool insert(const T& value)
        {
            node_type* parent = nullptr;
            node_type* current = root_;
            bool attach_left = false;

            while (current != nullptr)
            {
                parent = current;

                if (compare_(value, current->value))
                {
                    attach_left = true;
                    current = current->left;
                }
                else if (compare_(current->value, value))
                {
                    attach_left = false;
                    current = current->right;
                }
                else
                {
                    return false;
                }
            }

            // 分配可能抛出异常；此前树结构未被修改，因此插入提供强异常保证。
            node_type* created = new node_type(value, parent);
            if (parent == nullptr)
            {
                root_ = created;
            }
            else if (attach_left)
            {
                parent->left = created;
            }
            else
            {
                parent->right = created;
            }

            ++size_;

            // 新节点为叶子，从其父节点开始逐层修复即可覆盖所有受影响节点。
            node_type* walk = parent;
            while (walk != nullptr)
            {
                walk = rebalance(walk)->parent;
            }

            return true;
        }

        bool erase(const T& value)
        {
            node_type* target = locate(value);
            if (target == nullptr)
            {
                return false;
            }

            erase_node(target);
            return true;
        }

        bool contains(const T& value) const
        {
            return locate(value) != nullptr;
        }

        const T* find(const T& value) const
        {
            const node_type* found = locate(value);
            return found != nullptr ? &found->value : nullptr;
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

        // 最大的不超过 value 的元素。
        const T* floor(const T& value) const
        {
            const node_type* candidate = nullptr;
            const node_type* current = root_;

            while (current != nullptr)
            {
                if (compare_(current->value, value))
                {
                    candidate = current;
                    current = current->right;
                }
                else if (compare_(value, current->value))
                {
                    current = current->left;
                }
                else
                {
                    return &current->value;
                }
            }

            return candidate != nullptr ? &candidate->value : nullptr;
        }

        // 最小的不小于 value 的元素。
        const T* ceil(const T& value) const
        {
            const node_type* candidate = nullptr;
            const node_type* current = root_;

            while (current != nullptr)
            {
                if (compare_(value, current->value))
                {
                    candidate = current;
                    current = current->left;
                }
                else if (compare_(current->value, value))
                {
                    current = current->right;
                }
                else
                {
                    return &current->value;
                }
            }

            return candidate != nullptr ? &candidate->value : nullptr;
        }

        // 中序遍历结果；使用 Compare 排序时即为容器定义的升序。
        std::vector<T> to_vector() const
        {
            std::vector<T> values;
            values.reserve(size_);
            append_inorder(root_, values);
            return values;
        }

        int height() const
        {
            return root_ != nullptr ? root_->height : 0;
        }

        // 同时验证中序严格有序、父子指针互指、每个节点高度与平衡因子。
        bool validate() const
        {
            if (root_ == nullptr)
            {
                return size_ == 0;
            }

            if (root_->parent != nullptr)
            {
                return false;
            }

            const node_type* previous = nullptr;
            size_type count = 0;
            if (!validate_subtree(root_, previous, count))
            {
                return false;
            }

            return count == size_;
        }

    private:
        using node_type = node::AVLNode<T>;

        // 左旋：current 的右孩子成为子树根，必须是右重的。
        node_type* rotate_left(node_type* current) noexcept
        {
            node_type* pivot = current->right;
            current->right = pivot->left;

            if (pivot->left != nullptr)
            {
                pivot->left->parent = current;
            }

            pivot->parent = current->parent;
            if (current->parent == nullptr)
            {
                root_ = pivot;
            }
            else if (current->parent->left == current)
            {
                current->parent->left = pivot;
            }
            else
            {
                current->parent->right = pivot;
            }

            pivot->left = current;
            current->parent = pivot;

            current->pull();
            pivot->pull();
            return pivot;
        }

        // 右旋：current 的左孩子成为子树根，必须是左重的。
        node_type* rotate_right(node_type* current) noexcept
        {
            node_type* pivot = current->left;
            current->left = pivot->right;

            if (pivot->right != nullptr)
            {
                pivot->right->parent = current;
            }

            pivot->parent = current->parent;
            if (current->parent == nullptr)
            {
                root_ = pivot;
            }
            else if (current->parent->left == current)
            {
                current->parent->left = pivot;
            }
            else
            {
                current->parent->right = pivot;
            }

            pivot->right = current;
            current->parent = pivot;

            current->pull();
            pivot->pull();
            return pivot;
        }

        // 先重算当前高度，再按平衡因子选择单旋或双旋；返回修复后子树根。
        node_type* rebalance(node_type* current) noexcept
        {
            current->pull();
            const int factor = current->balance_factor();

            if (factor > 1)
            {
                if (current->left->balance_factor() < 0)
                {
                    rotate_left(current->left);
                }
                return rotate_right(current);
            }

            if (factor < -1)
            {
                if (current->right->balance_factor() > 0)
                {
                    rotate_right(current->right);
                }
                return rotate_left(current);
            }

            return current;
        }

        // 查找节点；返回值仅在容器未被修改时有效。
        node_type* locate(const T& value) const
        {
            node_type* current = root_;
            while (current != nullptr)
            {
                if (compare_(value, current->value))
                {
                    current = current->left;
                }
                else if (compare_(current->value, value))
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

        // 删除目标节点：双孩子情况把后继节点原位搬到目标位置，避免移动元素值。
        // 随后从最低的受影响父节点开始，沿 parent 指针一直修复到根。
        void erase_node(node_type* target)
        {
            node_type* repair_start = nullptr;

            if (target->left != nullptr && target->right != nullptr)
            {
                node_type* successor = target->right;
                while (successor->left != nullptr)
                {
                    successor = successor->left;
                }

                node_type* successor_parent = successor->parent;

                if (successor != target->right)
                {
                    // 后继最多只有右孩子；先用它顶替后继原来的位置。
                    successor_parent->left = successor->right;
                    if (successor->right != nullptr)
                    {
                        successor->right->parent = successor_parent;
                    }

                    successor->right = target->right;
                    target->right->parent = successor;
                }

                successor->left = target->left;
                target->left->parent = successor;
                successor->parent = target->parent;

                if (target->parent == nullptr)
                {
                    root_ = successor;
                }
                else if (target->parent->left == target)
                {
                    target->parent->left = successor;
                }
                else
                {
                    target->parent->right = successor;
                }

                // 后继不是右孩子时，须从原父节点开始修复；否则直接从后继开始。
                repair_start = successor != target->right ? successor_parent : successor;
            }
            else
            {
                node_type* child = target->left != nullptr ? target->left : target->right;
                node_type* parent = target->parent;

                if (child != nullptr)
                {
                    child->parent = parent;
                }

                if (parent == nullptr)
                {
                    root_ = child;
                }
                else if (parent->left == target)
                {
                    parent->left = child;
                }
                else
                {
                    parent->right = child;
                }

                repair_start = parent;
            }

            delete target;
            --size_;

            node_type* walk = repair_start;
            while (walk != nullptr)
            {
                walk = rebalance(walk)->parent;
            }
        }

        static void append_inorder(const node_type* current, std::vector<T>& values)
        {
            if (current == nullptr)
            {
                return;
            }

            append_inorder(current->left, values);
            values.push_back(current->value);
            append_inorder(current->right, values);
        }

        bool validate_subtree(const node_type* current, const node_type*& previous, size_type& count) const
        {
            if (current == nullptr)
            {
                return true;
            }

            if (current->left == current || current->right == current)
            {
                return false;
            }

            if (current->left != nullptr && current->left->parent != current)
            {
                return false;
            }

            if (current->right != nullptr && current->right->parent != current)
            {
                return false;
            }

            if (!validate_subtree(current->left, previous, count))
            {
                return false;
            }

            if (previous != nullptr && !compare_(previous->value, current->value))
            {
                return false;
            }

            const int left_height = node_type::height_of(current->left);
            const int right_height = node_type::height_of(current->right);
            const int expected_height = 1 + (left_height > right_height ? left_height : right_height);
            if (current->height != expected_height)
            {
                return false;
            }

            const int factor = left_height - right_height;
            if (factor < -1 || factor > 1)
            {
                return false;
            }

            previous = current;
            ++count;
            return validate_subtree(current->right, previous, count);
        }

        node_type* root_ = nullptr;
        size_type size_ = 0;
        Compare compare_{};
    };
}
