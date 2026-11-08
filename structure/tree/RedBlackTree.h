#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "../node/RBNode.h"

namespace coordination::tree
{
    // 红黑树容器（header-only，模板全部内联）。
    //
    // 设计约定：
    //  - 等价判定统一为 !comp(a, b) && !comp(b, a)，与 std::set 一致；
    //  - 节点由容器 new/delete 管理，find/floor/ceil 返回节点内部值的裸指针，
    //    任何插入、删除、clear 之后都会失效；
    //  - 空指针就是 CLRS 的 nil 哨兵，一律按黑色处理，因此不额外分配哨兵节点；
    //  - 删除双孩子节点时先用中序后继的值覆盖待删节点，再删除后继，
    //    把问题归结为「删除至多一个孩子的节点」，避免搬移指针时出错。
    //
    // 不变量（validate() 逐条校验）：
    //  1. 中序有序：左子树 < 本节点 < 右子树；
    //  2. 子节点的 parent 指回父节点，根节点的 parent 为空；
    //  3. 根为黑；
    //  4. 红节点的两个孩子都是黑（无红红相连）；
    //  5. 任一节点到其所有空叶的路径黑高相同。
    template <typename T, typename Compare = std::less<T>>
    class RedBlackTree
    {
    public:
        using value_type = T;
        using size_type = std::size_t;
        using key_compare = Compare;

        RedBlackTree() = default;

        explicit RedBlackTree(Compare comp)
            : comp_(std::move(comp))
        {
        }

        RedBlackTree(const RedBlackTree&) = delete;
        RedBlackTree& operator=(const RedBlackTree&) = delete;

        ~RedBlackTree()
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

        void clear() noexcept
        {
            destroy_subtree(root_);
            root_ = nullptr;
            size_ = 0;
        }

        // 插入：已存在等价元素时返回 false，树保持不变。
        bool insert(const T& value)
        {
            node_type* parent = nullptr;
            node_type* current = root_;
            bool attach_left = false;

            while (current != nullptr)
            {
                parent = current;
                if (comp_(value, current->value))
                {
                    attach_left = true;
                    current = current->left;
                }
                else if (comp_(current->value, value))
                {
                    attach_left = false;
                    current = current->right;
                }
                else
                {
                    return false;
                }
            }

            node_type* inserted = new node_type(value, color_type::red, parent);
            if (parent == nullptr)
            {
                root_ = inserted;
            }
            else if (attach_left)
            {
                parent->left = inserted;
            }
            else
            {
                parent->right = inserted;
            }

            ++size_;
            insert_fixup(inserted);
            return true;
        }

        // 删除：不存在等价元素时返回 false。
        bool erase(const T& value)
        {
            // find_node 是 const 接口，这里需要可写节点；节点由本容器独占，转换安全。
            node_type* target = const_cast<node_type*>(find_node(root_, value));
            if (target == nullptr)
            {
                return false;
            }

            // 双孩子：用中序后继的值覆盖，再转而删除后继（后继一定没有左孩子）。
            if (target->left != nullptr && target->right != nullptr)
            {
                node_type* successor = subtree_min(target->right);
                target->value = successor->value;
                target = successor;
            }

            erase_node(target);
            return true;
        }

        bool contains(const T& value) const
        {
            return find_node(root_, value) != nullptr;
        }

        const T* find(const T& value) const
        {
            const node_type* node = find_node(root_, value);
            return node != nullptr ? &node->value : nullptr;
        }

        const T* min() const
        {
            const node_type* node = subtree_min(root_);
            return node != nullptr ? &node->value : nullptr;
        }

        const T* max() const
        {
            const node_type* node = subtree_max(root_);
            return node != nullptr ? &node->value : nullptr;
        }

        // 最大的 <= value 的元素：一路记下最后一个满足 current <= value 的节点。
        const T* floor(const T& value) const
        {
            const node_type* current = root_;
            const node_type* best = nullptr;
            while (current != nullptr)
            {
                if (!comp_(value, current->value))
                {
                    best = current;
                    current = current->right;
                }
                else
                {
                    current = current->left;
                }
            }
            return best != nullptr ? &best->value : nullptr;
        }

        // 最小的 >= value 的元素。
        const T* ceil(const T& value) const
        {
            const node_type* current = root_;
            const node_type* best = nullptr;
            while (current != nullptr)
            {
                if (!comp_(current->value, value))
                {
                    best = current;
                    current = current->left;
                }
                else
                {
                    current = current->right;
                }
            }
            return best != nullptr ? &best->value : nullptr;
        }

        // 中序展开（升序）；迭代实现，避免递归受树高影响。
        std::vector<T> to_vector() const
        {
            std::vector<T> result;
            result.reserve(size_);

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
                result.push_back(current->value);
                current = current->right;
            }
            return result;
        }

        // 树高：空树 0，单节点 1。
        int height() const
        {
            return subtree_height(root_);
        }

        // 全量校验红黑树不变量，供测试与断言使用。
        bool validate() const
        {
            if (root_ == nullptr)
            {
                return size_ == 0;
            }
            if (root_->parent != nullptr || !root_->is_black())
            {
                return false;
            }

            bool ok = true;
            size_type node_count = 0;
            validate_subtree(root_, nullptr, nullptr, ok, node_count);
            return ok && node_count == size_;
        }

    private:
        using node_type = node::RBNode<T>;
        using color_type = node::NodeColor;

        node_type* root_ = nullptr;
        size_type size_ = 0;
        Compare comp_{};

        // 空指针按黑色计算，等价于 CLRS 的 nil 哨兵。
        static bool is_red(const node_type* node) noexcept
        {
            return node != nullptr && node->is_red();
        }

        static bool is_black(const node_type* node) noexcept
        {
            return node == nullptr || node->is_black();
        }

        static node_type* subtree_min(node_type* node) noexcept
        {
            while (node != nullptr && node->left != nullptr)
            {
                node = node->left;
            }
            return node;
        }

        static node_type* subtree_max(node_type* node) noexcept
        {
            while (node != nullptr && node->right != nullptr)
            {
                node = node->right;
            }
            return node;
        }

        static int subtree_height(const node_type* node) noexcept
        {
            if (node == nullptr)
            {
                return 0;
            }
            return 1 + std::max(subtree_height(node->left), subtree_height(node->right));
        }

        static void destroy_subtree(node_type* node) noexcept
        {
            if (node == nullptr)
            {
                return;
            }
            destroy_subtree(node->left);
            destroy_subtree(node->right);
            delete node;
        }

        // 在 start 子树中查找等价元素；找不到返回 nullptr。
        const node_type* find_node(const node_type* start, const T& value) const
        {
            const node_type* current = start;
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

        // 左旋：右孩子 y 上位，x 成为 y 的左孩子。
        void rotate_left(node_type* x) noexcept
        {
            node_type* y = x->right;
            x->right = y->left;
            if (y->left != nullptr)
            {
                y->left->parent = x;
            }

            y->parent = x->parent;
            if (x->parent == nullptr)
            {
                root_ = y;
            }
            else if (x == x->parent->left)
            {
                x->parent->left = y;
            }
            else
            {
                x->parent->right = y;
            }

            y->left = x;
            x->parent = y;
        }

        // 右旋：左孩子 y 上位，x 成为 y 的右孩子。
        void rotate_right(node_type* x) noexcept
        {
            node_type* y = x->left;
            x->left = y->right;
            if (y->right != nullptr)
            {
                y->right->parent = x;
            }

            y->parent = x->parent;
            if (x->parent == nullptr)
            {
                root_ = y;
            }
            else if (x == x->parent->right)
            {
                x->parent->right = y;
            }
            else
            {
                x->parent->left = y;
            }

            y->right = x;
            x->parent = y;
        }

        // 插入修复：新节点为红，唯一可能被破坏的不变量是「红红相连」。
        void insert_fixup(node_type* node) noexcept
        {
            while (node->parent != nullptr && node->parent->is_red())
            {
                node_type* parent = node->parent;
                node_type* grand = parent->parent; // 父为红 ⇒ 父非根 ⇒ grand 非空

                if (parent == grand->left)
                {
                    node_type* uncle = grand->right;
                    if (is_red(uncle))
                    {
                        // 情况 1：叔叔为红。父辈与叔叔染黑、祖父染红，冲突上移两层。
                        parent->color = color_type::black;
                        uncle->color = color_type::black;
                        grand->color = color_type::red;
                        node = grand;
                    }
                    else
                    {
                        // 情况 2：自己是内侧孩子，先绕父左旋，转成情况 3。
                        if (node == parent->right)
                        {
                            node = parent;
                            rotate_left(node);
                            parent = node->parent;
                        }

                        // 情况 3：自己是外侧孩子，绕祖父右旋并重新着色。
                        parent->color = color_type::black;
                        grand->color = color_type::red;
                        rotate_right(grand);
                    }
                }
                else
                {
                    node_type* uncle = grand->left;
                    if (is_red(uncle))
                    {
                        parent->color = color_type::black;
                        uncle->color = color_type::black;
                        grand->color = color_type::red;
                        node = grand;
                    }
                    else
                    {
                        if (node == parent->left)
                        {
                            node = parent;
                            rotate_right(node);
                            parent = node->parent;
                        }

                        parent->color = color_type::black;
                        grand->color = color_type::red;
                        rotate_left(grand);
                    }
                }
            }

            root_->color = color_type::black; // 插入后根节点必定存在，根恒为黑
        }

        // 删除至多只有一个孩子的节点，并在删掉黑节点时启动修复。
        void erase_node(node_type* node) noexcept
        {
            node_type* child = node->left != nullptr ? node->left : node->right;
            node_type* parent = node->parent;
            const bool removed_black = node->is_black();

            if (child != nullptr)
            {
                child->parent = parent;
            }
            if (parent == nullptr)
            {
                root_ = child;
            }
            else if (node == parent->left)
            {
                parent->left = child;
            }
            else
            {
                parent->right = child;
            }

            delete node;
            --size_;

            if (removed_black)
            {
                erase_fixup(child, parent);
            }
        }

        // 删除修复：x 是被删节点的孩子（可能为空指针），parent 是 x 的新父节点。
        // 空指针视为黑色，所以「黑高少一」的缺陷会向上传递，直到落到红节点或根为止。
        void erase_fixup(node_type* x, node_type* parent) noexcept
        {
            while (x != root_ && is_black(x))
            {
                if (parent == nullptr)
                {
                    break; // 防御：正常情况下 x 非根时 parent 必定存在
                }

                if (x == parent->left)
                {
                    node_type* sibling = parent->right;

                    // 情况 1：兄弟为红。兄弟染黑、父染红，绕父左旋后兄弟转为黑。
                    if (is_red(sibling))
                    {
                        sibling->color = color_type::black;
                        parent->color = color_type::red;
                        rotate_left(parent);
                        sibling = parent->right;
                    }

                    if (sibling == nullptr)
                    {
                        // 兄弟为空等价于两个黑孩子，缺陷上移。
                        x = parent;
                        parent = x->parent;
                        continue;
                    }

                    // 情况 2：兄弟的两个孩子都黑。兄弟染红，缺陷上移到父节点。
                    if (is_black(sibling->left) && is_black(sibling->right))
                    {
                        sibling->color = color_type::red;
                        x = parent;
                        parent = x->parent;
                    }
                    else
                    {
                        // 情况 3：兄弟的远侄为黑（近侄必为红），先绕兄弟右旋，转成情况 4。
                        if (is_black(sibling->right))
                        {
                            if (sibling->left != nullptr)
                            {
                                sibling->left->color = color_type::black;
                            }
                            sibling->color = color_type::red;
                            rotate_right(sibling);
                            sibling = parent->right;
                        }

                        // 情况 4：兄弟继承父色，父与远侄染黑，绕父左旋后缺陷消除。
                        sibling->color = parent->color;
                        parent->color = color_type::black;
                        if (sibling->right != nullptr)
                        {
                            sibling->right->color = color_type::black;
                        }
                        rotate_left(parent);
                        x = root_;
                        parent = nullptr;
                    }
                }
                else
                {
                    node_type* sibling = parent->left;

                    // 情况 1（镜像）：兄弟为红。
                    if (is_red(sibling))
                    {
                        sibling->color = color_type::black;
                        parent->color = color_type::red;
                        rotate_right(parent);
                        sibling = parent->left;
                    }

                    if (sibling == nullptr)
                    {
                        x = parent;
                        parent = x->parent;
                        continue;
                    }

                    // 情况 2（镜像）：兄弟的两个孩子都黑。
                    if (is_black(sibling->left) && is_black(sibling->right))
                    {
                        sibling->color = color_type::red;
                        x = parent;
                        parent = x->parent;
                    }
                    else
                    {
                        // 情况 3（镜像）：兄弟的远侄（左孩子）为黑。
                        if (is_black(sibling->left))
                        {
                            if (sibling->right != nullptr)
                            {
                                sibling->right->color = color_type::black;
                            }
                            sibling->color = color_type::red;
                            rotate_left(sibling);
                            sibling = parent->left;
                        }

                        // 情况 4（镜像）。
                        sibling->color = parent->color;
                        parent->color = color_type::black;
                        if (sibling->left != nullptr)
                        {
                            sibling->left->color = color_type::black;
                        }
                        rotate_right(parent);
                        x = root_;
                        parent = nullptr;
                    }
                }
            }

            if (x != nullptr)
            {
                x->color = color_type::black;
            }
        }

        // 递归校验子树，返回该子树的黑高（含自身）。
        // lower/upper 是祖先给出的开区间边界：lower < value < upper。
        int validate_subtree(const node_type* node, const T* lower, const T* upper, bool& ok, size_type& node_count) const
        {
            if (!ok)
            {
                return 0;
            }
            if (node == nullptr)
            {
                return 1; // 空叶计为黑，黑高 1
            }

            ++node_count;
            if (lower != nullptr && !comp_(*lower, node->value))
            {
                ok = false;
                return 0;
            }
            if (upper != nullptr && !comp_(node->value, *upper))
            {
                ok = false;
                return 0;
            }
            if (node->left != nullptr && node->left->parent != node)
            {
                ok = false;
                return 0;
            }
            if (node->right != nullptr && node->right->parent != node)
            {
                ok = false;
                return 0;
            }
            if (node->is_red() && (is_red(node->left) || is_red(node->right)))
            {
                ok = false;
                return 0;
            }

            const int left_black_height = validate_subtree(node->left, lower, &node->value, ok, node_count);
            if (!ok)
            {
                return 0;
            }
            const int right_black_height = validate_subtree(node->right, &node->value, upper, ok, node_count);
            if (!ok || left_black_height != right_black_height)
            {
                ok = false;
                return 0;
            }
            return left_black_height + (node->is_black() ? 1 : 0);
        }
    };
}
