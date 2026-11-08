#pragma once

#include <cstddef>
#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../node/HashNode.h"
#include "../pool/ObjectPool.h"

namespace coordination::lru
{
    // 带 LRU 淘汰的无序集合：链地址哈希表 + 侵入式双向 LRU 链。
    //
    // 每个元素只分配一个 Entry（来自 ObjectPool），Entry 同时挂在桶链和 LRU 链上：
    //   Entry 继承 HashNode<T>，next 指向同一桶里的后继节点；
    //   newer / older 指向更近 / 更久使用的节点。
    // 最近使用的节点在 newest_，最久未使用的节点在 oldest_，淘汰时从链尾摘除。
    // 负载因子超过 3/4 时桶数翻倍；rehash 只重建桶链，不改变 LRU 顺序。
    // T 需要可拷贝；Hash 与 Equal 的语义应与 std::hash / std::equal_to 一致。
    template <typename T, typename Hash = std::hash<T>, typename Equal = std::equal_to<T>>
    class LeastRarelyUsageUnorderedSet
    {
    public:
        using value_type = T;
        using size_type = std::size_t;

        // capacity 为 0 表示不限制元素数量。
        explicit LeastRarelyUsageUnorderedSet(size_type capacity = 0)
            : capacity_(capacity)
        {
        }

        LeastRarelyUsageUnorderedSet(const LeastRarelyUsageUnorderedSet&) = delete;
        LeastRarelyUsageUnorderedSet& operator=(const LeastRarelyUsageUnorderedSet&) = delete;

        ~LeastRarelyUsageUnorderedSet()
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

        size_type capacity() const noexcept
        {
            return capacity_;
        }

        // 调整元素上限；缩小时从最久未使用的一端开始淘汰，0 表示取消限制。
        void set_capacity(size_type capacity)
        {
            capacity_ = capacity;
            while (capacity_ != 0 && size_ > capacity_)
            {
                evict_one();
            }
        }

        // 不存在时插入并返回 true；已存在时提升为最近使用并返回 false。
        // 容量已满时先淘汰最久未使用的元素，再插入新元素。
        bool insert(const T& value)
        {
            const size_type hash_value = hash_(value);
            Entry* existing = find_entry(value, hash_value);
            if (existing != nullptr)
            {
                make_newest(existing);
                return false;
            }

            const size_type projected = (capacity_ != 0 && size_ >= capacity_) ? size_ : size_ + 1;
            ensure_bucket_capacity(projected);

            Entry* entry = pool_.create(value, hash_value);

            if (capacity_ != 0 && size_ >= capacity_)
            {
                evict_one();
            }

            link_bucket(entry);
            link_newest(entry);
            ++size_;
            return true;
        }

        bool erase(const T& value)
        {
            Entry* entry = find_entry(value, hash_(value));
            if (entry == nullptr)
            {
                return false;
            }

            unlink_bucket(entry);
            unlink_lru(entry);
            pool_.destroy(entry);
            --size_;
            return true;
        }

        bool contains(const T& value) const
        {
            return find_entry(value, hash_(value)) != nullptr;
        }

        // 提升为最近使用；元素不存在时返回 false。
        bool touch(const T& value)
        {
            Entry* entry = find_entry(value, hash_(value));
            if (entry == nullptr)
            {
                return false;
            }

            make_newest(entry);
            return true;
        }

        const T* find(const T& value) const
        {
            const Entry* entry = find_entry(value, hash_(value));
            return entry != nullptr ? &entry->value : nullptr;
        }

        // 淘汰最久未使用的元素；空集返回 false。
        bool evict_one()
        {
            if (oldest_ == nullptr)
            {
                return false;
            }

            Entry* entry = oldest_;
            unlink_bucket(entry);
            unlink_lru(entry);
            pool_.destroy(entry);
            --size_;
            return true;
        }

        void clear() noexcept
        {
            for (Entry*& bucket : buckets_)
            {
                bucket = nullptr;
            }

            newest_ = nullptr;
            oldest_ = nullptr;
            size_ = 0;
            pool_.clear();
        }

        // 按最近使用到最久未使用的顺序返回元素。
        std::vector<T> to_vector() const
        {
            std::vector<T> values;
            values.reserve(size_);
            for (const Entry* entry = newest_; entry != nullptr; entry = entry->older)
            {
                values.push_back(entry->value);
            }
            return values;
        }

        // 自检：桶链归属与计数、LRU 链双向一致性、节点集合一致、值无重复。
        bool validate() const
        {
            if (size_ == 0)
            {
                if (newest_ != nullptr || oldest_ != nullptr)
                {
                    return false;
                }
            }
            else if (newest_ == nullptr || oldest_ == nullptr)
            {
                return false;
            }

            if (pool_.alive() != size_)
            {
                return false;
            }

            if (buckets_.empty())
            {
                if (size_ != 0)
                {
                    return false;
                }
            }
            else
            {
                const size_type bucket_count = buckets_.size();
                if ((bucket_count & (bucket_count - 1)) != 0)
                {
                    return false;
                }
            }

            // 从最近到最久遍历 LRU 链，同时检查前驱指针与环。
            size_type lru_count = 0;
            const Entry* previous = nullptr;
            for (const Entry* entry = newest_; entry != nullptr; entry = entry->older)
            {
                if (entry->newer != previous)
                {
                    return false;
                }

                if (++lru_count > size_)
                {
                    return false;
                }

                previous = entry;
            }

            if (lru_count != size_ || previous != oldest_)
            {
                return false;
            }

            if (oldest_ != nullptr && oldest_->older != nullptr)
            {
                return false;
            }

            // 遍历所有桶链，检查缓存哈希落在正确的桶、节点不重复。
            std::unordered_set<const Entry*> bucket_nodes;
            bucket_nodes.reserve(size_);
            if (!buckets_.empty())
            {
                const size_type mask = buckets_.size() - 1;
                for (size_type index = 0; index < buckets_.size(); ++index)
                {
                    for (const Entry* entry = buckets_[index]; entry != nullptr;
                         entry = static_cast<const Entry*>(entry->next))
                    {
                        if ((entry->hash & mask) != index)
                        {
                            return false;
                        }

                        if (!bucket_nodes.insert(entry).second)
                        {
                            return false;
                        }
                    }
                }
            }

            if (bucket_nodes.size() != size_)
            {
                return false;
            }

            // 哈希表与 LRU 链必须包含同一批节点，并且存的值互不相同。
            std::unordered_set<T, Hash, Equal> values(0, hash_, equal_);
            values.reserve(size_);
            for (const Entry* entry = newest_; entry != nullptr; entry = entry->older)
            {
                if (bucket_nodes.find(entry) == bucket_nodes.end())
                {
                    return false;
                }

                if (!values.insert(entry->value).second)
                {
                    return false;
                }
            }

            return true;
        }

        // 对象池当前存活的节点数；用于确认 clear 之后没有残留节点。
        size_type pool_alive() const noexcept
        {
            return pool_.alive();
        }

    private:
        // 同时挂两条链的节点：HashNode 提供值、桶内 next 和缓存哈希，
        // newer / older 提供侵入式 LRU 链，整节点只从对象池分配一次。
        struct Entry : coordination::node::HashNode<T>
        {
            Entry* newer = nullptr; // 更近使用的节点
            Entry* older = nullptr; // 更久未使用的节点

            Entry(const T& value, size_type hash)
                : coordination::node::HashNode<T>(value, nullptr, hash)
            {
            }
        };

        using pool_type = coordination::pool::ObjectPool<Entry>;

        static constexpr size_type kInitialBucketCount = 8;

        Entry* find_entry(const T& value, size_type hash_value) const
        {
            if (buckets_.empty())
            {
                return nullptr;
            }

            Entry* entry = buckets_[hash_value & (buckets_.size() - 1)];
            while (entry != nullptr)
            {
                if (entry->hash == hash_value && equal_(entry->value, value))
                {
                    return entry;
                }

                entry = static_cast<Entry*>(entry->next);
            }
            return nullptr;
        }

        void link_bucket(Entry* entry) noexcept
        {
            const size_type index = entry->hash & (buckets_.size() - 1);
            entry->next = buckets_[index];
            buckets_[index] = entry;
        }

        void unlink_bucket(Entry* entry) noexcept
        {
            const size_type index = entry->hash & (buckets_.size() - 1);
            Entry* current = buckets_[index];
            if (current == entry)
            {
                buckets_[index] = static_cast<Entry*>(entry->next);
                return;
            }

            while (current != nullptr)
            {
                Entry* next = static_cast<Entry*>(current->next);
                if (next == entry)
                {
                    current->next = entry->next;
                    return;
                }

                current = next;
            }
        }

        // 把节点挂到 LRU 链的最近端。
        void link_newest(Entry* entry) noexcept
        {
            entry->newer = nullptr;
            entry->older = newest_;
            if (newest_ != nullptr)
            {
                newest_->newer = entry;
            }
            else
            {
                oldest_ = entry;
            }
            newest_ = entry;
        }

        // 把节点从 LRU 链上摘下，并修好首尾指针。
        void unlink_lru(Entry* entry) noexcept
        {
            if (entry->older != nullptr)
            {
                entry->older->newer = entry->newer;
            }
            else
            {
                oldest_ = entry->newer;
            }

            if (entry->newer != nullptr)
            {
                entry->newer->older = entry->older;
            }
            else
            {
                newest_ = entry->older;
            }

            entry->newer = nullptr;
            entry->older = nullptr;
        }

        void make_newest(Entry* entry) noexcept
        {
            if (entry == newest_)
            {
                return;
            }

            unlink_lru(entry);
            link_newest(entry);
        }

        // 重建桶数组；LRU 链保持不变。
        void rehash(size_type bucket_count)
        {
            std::vector<Entry*> buckets(bucket_count, nullptr);
            const size_type mask = bucket_count - 1;
            for (Entry* entry = newest_; entry != nullptr; entry = entry->older)
            {
                const size_type index = entry->hash & mask;
                entry->next = buckets[index];
                buckets[index] = entry;
            }
            buckets_.swap(buckets);
        }

        void ensure_bucket_capacity(size_type projected_size)
        {
            if (buckets_.empty())
            {
                rehash(kInitialBucketCount);
                return;
            }

            // 超过 3/4 负载因子就把桶数翻倍。
            if (projected_size > buckets_.size() - buckets_.size() / 4)
            {
                rehash(buckets_.size() * 2);
            }
        }

        std::vector<Entry*> buckets_;
        Entry* newest_ = nullptr;
        Entry* oldest_ = nullptr;
        size_type size_ = 0;
        size_type capacity_ = 0;
        Hash hash_{};
        Equal equal_{};
        pool_type pool_;
    };
}
