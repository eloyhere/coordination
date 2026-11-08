#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "../node/DoubleLinearNode.h"
#include "../pool/ObjectPool.h"

namespace coordination::lru
{
    // LRU（最近最少使用）链表：头部为最近使用端，尾部为最久未使用端。
    //
    // 哨兵 sentinel_ 与首尾节点构成循环双向链表，首次 push 时就地构造、clear 时销毁，
    // 既不占用对象池槽位，也不要求 T 可默认构造。
    // 数据节点全部由 ObjectPool<DoubleLinearNode<T>> 分配，push/erase 不直接 new/delete。
    // 容量为 0 表示不限容量；调小容量时按“最久未使用优先”淘汰。
    // 句柄为池槽位的 {index, generation}：erase / 淘汰 / clear 会让旧句柄立即失效，
    // 之后即使同一槽位被新 push 复用，旧句柄也不会再指向新元素。
    // 句柄不携带池身份，跨容器使用属于未定义用法（同索引同代际无法区分）。
    // T 需可拷贝。
    template <typename T>
    class LeastRarelyUsageList
    {
    public:
        using value_type = T;
        using size_type = std::size_t;
        using node_type = node::DoubleLinearNode<T>;
        using pool_type = pool::ObjectPool<node_type>;
        using handle_type = typename pool_type::SlotHandle;

        explicit LeastRarelyUsageList(size_type capacity = 0)
            : capacity_(capacity)
        {
        }

        LeastRarelyUsageList(const LeastRarelyUsageList&) = delete;
        LeastRarelyUsageList& operator=(const LeastRarelyUsageList&) = delete;

        // 释放全部数据节点与哨兵。
        ~LeastRarelyUsageList()
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

        // 当前容量上限，0 表示不限容量。
        size_type capacity() const noexcept
        {
            return capacity_;
        }

        // 调整容量：缩小时从尾部（最久未使用）逐个淘汰。O(k)，k 为淘汰个数。
        void set_capacity(size_type capacity)
        {
            capacity_ = capacity;
            while (capacity_ != 0 && size_ > capacity_)
            {
                evict_least_recent();
            }
        }

        // 压入新元素到最近使用端，返回其句柄；容量满时先淘汰最久未使用。O(1)。
        // 先分配节点再淘汰，保证分配失败时不会丢失原有元素。
        handle_type push(const T& value)
        {
            ensure_sentinel(value);
            node_type* node = pool_.create(value);
            while (capacity_ != 0 && size_ >= capacity_)
            {
                evict_least_recent();
            }

            node_type* head = &*sentinel_;
            head->link_after(node);
            ++size_;
            return pool_.make_handle(node);
        }

        // 标记为最近使用：先用池的 resolve 做 O(1) 校验，命中后 O(1) 摘除并头插。
        // 已失效句柄（erase / 淘汰 / clear 之后）返回 false。
        bool touch(handle_type handle)
        {
            node_type* node = pool_.resolve(handle);
            if (node == nullptr)
            {
                return false;
            }

            node_type* head = &*sentinel_;
            if (head->next != node)
            {
                node->unlink();
                head->link_after(node);
            }

            return true;
        }

        // 删除句柄对应节点：O(1) 校验与摘除，返回是否命中；失效句柄返回 false。
        bool erase(handle_type handle)
        {
            node_type* node = pool_.resolve(handle);
            if (node == nullptr)
            {
                return false;
            }

            node->unlink();
            pool_.destroy(node);
            --size_;
            return true;
        }

        // 句柄对应的元素地址；失效句柄返回 nullptr。O(1)。
        // 返回的地址在对应元素被删除后失效，不应继续持有。
        T* value(handle_type handle) noexcept
        {
            node_type* node = pool_.resolve(handle);
            return node == nullptr ? nullptr : &node->value;
        }

        // 最久未使用元素的地址；空表返回 nullptr。
        // 返回的地址在容器修改后可能失效，仅适合立即解引用。
        T* least_recent() noexcept
        {
            return size_ == 0 ? nullptr : &sentinel_->prev->value;
        }

        // 最近使用元素的地址；空表返回 nullptr。
        // 返回的地址在容器修改后可能失效，仅适合立即解引用。
        T* most_recent() noexcept
        {
            return size_ == 0 ? nullptr : &sentinel_->next->value;
        }

        // 释放全部数据节点并销毁哨兵。所有旧句柄失效。
        void clear() noexcept
        {
            if (sentinel_.has_value())
            {
                node_type* self = &*sentinel_;
                node_type* current = self->next;
                while (current != self)
                {
                    node_type* next = current->next;
                    pool_.destroy(current);
                    current = next;
                }

                sentinel_.reset();
            }

            size_ = 0;
        }

        // 按“最近 → 最久”顺序复制元素。
        std::vector<T> to_vector() const
        {
            std::vector<T> result;
            result.reserve(size_);
            if (size_ == 0)
            {
                return result;
            }

            const node_type* self = &*sentinel_;
            for (const node_type* current = self->next; current != self; current = current->next)
            {
                result.push_back(current->value);
            }

            return result;
        }

        // 自检：哨兵自环关系、双向链接、计数、池内存活数，以及每个节点确由本池分配。
        // O(n)。
        bool validate() const
        {
            if (!sentinel_.has_value())
            {
                return size_ == 0 && pool_.alive() == 0;
            }

            const node_type* self = &*sentinel_;
            if (self->next == nullptr || self->prev == nullptr)
            {
                return false;
            }

            if (self->next->prev != self || self->prev->next != self)
            {
                return false;
            }

            size_type count = 0;
            const node_type* previous = self;
            const node_type* current = self->next;
            while (current != self)
            {
                if (current == nullptr || current->prev != previous || current->next == nullptr)
                {
                    return false;
                }

                if (current->next->prev != current)
                {
                    return false;
                }

                // owns() 证明节点地址属于本池；节点仍在链表中即当前存活。
                if (!pool_.owns(current))
                {
                    return false;
                }

                previous = current;
                current = current->next;
                if (++count > size_)
                {
                    return false;
                }
            }

            if (previous != self->prev)
            {
                return false;
            }

            return count == size_ && pool_.alive() == size_;
        }

        // 池中存活节点数，正常应恒等于 size()；仅供自检与测试观察池状态。
        size_type pool_alive() const noexcept
        {
            return pool_.alive();
        }

    private:
        // 哨兵按需构造：占位 value 复制首个元素，仅用于满足节点构造，不参与容器语义。
        void ensure_sentinel(const T& value)
        {
            if (sentinel_.has_value())
            {
                return;
            }

            sentinel_.emplace(value);
            node_type* self = &*sentinel_;
            self->next = self;
            self->prev = self;
        }

        // 摘除并销毁尾部（最久未使用）节点。O(1)。
        void evict_least_recent() noexcept
        {
            if (!sentinel_.has_value())
            {
                return;
            }

            node_type* self = &*sentinel_;
            node_type* victim = self->prev;
            if (victim == self)
            {
                return;
            }

            victim->unlink();
            pool_.destroy(victim);
            --size_;
        }

        std::optional<node_type> sentinel_;
        pool_type pool_;
        size_type size_ = 0;
        size_type capacity_ = 0;
    };
}
