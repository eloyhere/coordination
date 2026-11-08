#pragma once

#include "../node/DoubleLinearNode.h"
#include "../pool/ObjectPool.h"
#include "../tree/AVLTree.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace coordination::lru
{
    // 带 LRU 淘汰的有序集合。
    //
    // 每个元素同时挂在两个结构上：
    //   * 有序索引：AVLTree<Entry, EntryCompare>，按 Compare 提供键升序迭代；
    //   * LRU 双向链：head_ 为最近使用、tail_ 为最久未使用，命中后前移。
    // 索引条目只保存节点内部值的稳定地址与节点指针，节点由 ObjectPool 分配，
    // 因此 find() 返回的地址在元素被删除或 clear() 之前始终有效。
    // 容量为 0 表示不限；容量满时先淘汰最久未使用节点，再插入新元素。
    template <typename T, typename Compare = std::less<T>>
    class LeaseRarelyUsageOrderedSet
    {
    public:
        using value_type = T;
        using size_type = std::size_t;

        explicit LeaseRarelyUsageOrderedSet(size_type capacity = 0)
            : index_(EntryCompare{})
            , capacity_(capacity)
        {
        }

        LeaseRarelyUsageOrderedSet(const LeaseRarelyUsageOrderedSet&) = delete;
        LeaseRarelyUsageOrderedSet& operator=(const LeaseRarelyUsageOrderedSet&) = delete;

        ~LeaseRarelyUsageOrderedSet()
        {
            clear();
        }

        bool empty() const noexcept
        {
            return index_.empty();
        }

        size_type size() const noexcept
        {
            return index_.size();
        }

        size_type capacity() const noexcept
        {
            return capacity_;
        }

        // 调整容量：0 表示不限；缩小时从最久未使用端淘汰直到满足容量。
        void set_capacity(size_type capacity)
        {
            capacity_ = capacity;
            while (capacity_ != 0 && index_.size() > capacity_)
            {
                evict_one();
            }
        }

        // 已存在：提升为最近使用并返回 false；容量满时先淘汰最久未使用。
        bool insert(const T& value)
        {
            Entry probe{&value, nullptr};
            const Entry* existing = index_.find(probe);
            if (existing != nullptr)
            {
                move_to_front(existing->node);
                return false;
            }

            if (capacity_ != 0 && index_.size() >= capacity_)
            {
                evict_one();
            }

            node_type* created = pool_.create(value);

            // 索引节点由 AVLTree 自行 new：失败时回收池节点，保持两处一致。
            try
            {
                if (!index_.insert(Entry{&created->value, created}))
                {
                    pool_.destroy(created);
                    return false;
                }
            }
            catch (...)
            {
                pool_.destroy(created);
                throw;
            }

            push_front(created);
            return true;
        }

        bool erase(const T& value)
        {
            Entry probe{&value, nullptr};
            const Entry* found = index_.find(probe);
            if (found == nullptr)
            {
                return false;
            }

            node_type* target = found->node;
            index_.erase(probe);
            unlink(target);
            pool_.destroy(target);
            return true;
        }

        bool contains(const T& value) const
        {
            Entry probe{&value, nullptr};
            return index_.find(probe) != nullptr;
        }

        // 提升为最近使用；不存在返回 false。
        bool touch(const T& value)
        {
            Entry probe{&value, nullptr};
            const Entry* entry = index_.find(probe);
            if (entry == nullptr)
            {
                return false;
            }

            move_to_front(entry->node);
            return true;
        }

        const T* find(const T& value) const
        {
            Entry probe{&value, nullptr};
            const Entry* entry = index_.find(probe);
            return entry != nullptr ? entry->value : nullptr;
        }

        // 淘汰最久未使用；空集返回 false。
        bool evict_one()
        {
            if (tail_ == nullptr)
            {
                return false;
            }

            node_type* victim = tail_;
            index_.erase(Entry{&victim->value, victim});
            unlink(victim);
            pool_.destroy(victim);
            return true;
        }

        void clear() noexcept
        {
            index_.clear();
            pool_.clear();
            head_ = nullptr;
            tail_ = nullptr;
        }

        // 按键升序（有序视图）。
        std::vector<T> to_vector() const
        {
            const std::vector<Entry> entries = index_.to_vector();
            std::vector<T> values;
            values.reserve(entries.size());
            for (const Entry& entry : entries)
            {
                values.push_back(*entry.value);
            }

            return values;
        }

        // 最近 → 最久。
        std::vector<T> lru_order() const
        {
            std::vector<T> values;
            values.reserve(index_.size());
            for (const node_type* current = head_; current != nullptr; current = current->next)
            {
                values.push_back(current->value);
            }

            return values;
        }

        // 同时验证有序索引、LRU 双向链与各处计数是否一致。
        bool validate() const
        {
            if (!index_.validate())
            {
                return false;
            }

            if (index_.size() != pool_.alive())
            {
                return false;
            }

            if ((head_ == nullptr) != (tail_ == nullptr))
            {
                return false;
            }

            if (head_ == nullptr)
            {
                return index_.empty();
            }

            if (head_->prev != nullptr || tail_->next != nullptr)
            {
                return false;
            }

            const node_type* previous = nullptr;
            const node_type* current = head_;
            size_type count = 0;

            while (current != nullptr)
            {
                if (current->prev != previous)
                {
                    return false;
                }

                Entry probe{&current->value, nullptr};
                const Entry* entry = index_.find(probe);
                if (entry == nullptr || entry->node != current)
                {
                    return false;
                }

                previous = current;
                current = current->next;
                ++count;

                // 链长不可能超过索引大小；超出说明链上出现环。
                if (count > index_.size())
                {
                    return false;
                }
            }

            return count == index_.size() && previous == tail_;
        }

        // 测试用诊断接口：对象池中存活的节点数，正常时恒等于 size()。
        size_type pool_alive() const noexcept
        {
            return pool_.alive();
        }

    private:
        using node_type = node::DoubleLinearNode<T>;

        // 有序索引条目：value 指向节点内部的稳定值地址，node 用于回找 LRU 节点。
        struct Entry
        {
            const T* value = nullptr;
            node_type* node = nullptr;
        };

        // 只按元素值比较，忽略节点地址；查找时可构造仅填 value 的临时条目。
        struct EntryCompare
        {
            Compare compare{};

            bool operator()(const Entry& lhs, const Entry& rhs) const
            {
                return compare(*lhs.value, *rhs.value);
            }
        };

        using index_type = tree::AVLTree<Entry, EntryCompare>;
        using pool_type = pool::ObjectPool<node_type>;

        // 头插到 LRU 链首，node 必须尚未入链。
        void push_front(node_type* node) noexcept
        {
            node->prev = nullptr;
            node->next = head_;

            if (head_ != nullptr)
            {
                head_->prev = node;
            }
            head_ = node;

            if (tail_ == nullptr)
            {
                tail_ = node;
            }
        }

        // 从 LRU 链摘下节点（不释放内存）。
        void unlink(node_type* node) noexcept
        {
            if (node->prev != nullptr)
            {
                node->prev->next = node->next;
            }
            else
            {
                head_ = node->next;
            }

            if (node->next != nullptr)
            {
                node->next->prev = node->prev;
            }
            else
            {
                tail_ = node->prev;
            }

            node->prev = nullptr;
            node->next = nullptr;
        }

        void move_to_front(node_type* node) noexcept
        {
            if (head_ == node)
            {
                return;
            }

            unlink(node);
            push_front(node);
        }

        pool_type pool_{};
        index_type index_;
        node_type* head_ = nullptr;
        node_type* tail_ = nullptr;
        size_type capacity_ = 0;
    };
}
