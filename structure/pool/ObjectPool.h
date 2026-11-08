#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace coordination::pool
{
    // 定长槽位对象池。
    //
    // 槽位只增不减：destroy 只调用析构并把槽位标记为空闲，create 优先复用空闲槽位。
    // 槽位地址稳定（vector<unique_ptr<Slot>>），因此外部持有的 T* 在池扩容后依然有效。
    // 每个槽位带代际计数：对象销毁时递增，SlotHandle 记录创建时的代际，
    // 从而识别“槽位已被复用”的失效句柄。
    template <typename T>
    class ObjectPool
    {
    public:
        // 池槽位句柄：index 为槽位下标，generation 为创建时的槽位代际。
        // 默认句柄（index = -1）表示无效；只有 index 与 generation 同时匹配才算存活。
        struct SlotHandle
        {
            std::size_t index = static_cast<std::size_t>(-1);
            std::uint32_t generation = 0;
        };

        ObjectPool() = default;
        ObjectPool(const ObjectPool&) = delete;
        ObjectPool& operator=(const ObjectPool&) = delete;

        ~ObjectPool()
        {
            clear();
        }

        template <typename... Args>
        T* create(Args&&... args)
        {
            std::size_t index = 0;
            if (!free_.empty())
            {
                index = free_.back();
                free_.pop_back();
            }
            else
            {
                auto slot = std::make_unique<Slot>();
                slot->index = slots_.size();
                slots_.push_back(std::move(slot));
                index = slots_.size() - 1;
            }

            Slot& slot = *slots_[index];
            ::new (static_cast<void*>(slot.storage)) T(std::forward<Args>(args)...);
            slot.in_use = true;
            ++alive_;
            return std::launder(reinterpret_cast<T*>(slot.storage));
        }

        // 调用析构并回收槽位；重复 destroy 或空指针是安全的空操作。
        // 回收时递增代际，使所有指向该槽位的旧句柄立即失效。
        void destroy(T* object) noexcept
        {
            if (object == nullptr)
            {
                return;
            }

            Slot* slot = reinterpret_cast<Slot*>(object);
            if (slot->index >= slots_.size() || slots_[slot->index].get() != slot || !slot->in_use)
            {
                return;
            }

            object->~T();
            slot->in_use = false;
            bump_generation(*slot);
            free_.push_back(slot->index);
            --alive_;
        }

        // 对所有存活对象调用析构，槽位全部回收（池本身保留）。所有旧句柄失效。
        void clear() noexcept
        {
            for (auto& slot : slots_)
            {
                if (slot->in_use)
                {
                    std::launder(reinterpret_cast<T*>(slot->storage))->~T();
                    slot->in_use = false;
                    bump_generation(*slot);
                }
            }

            free_.clear();
            free_.reserve(slots_.size());
            for (std::size_t i = 0; i < slots_.size(); ++i)
            {
                free_.push_back(i);
            }
            alive_ = 0;
        }

        // 由对象地址构造句柄；空指针、不属于本池或已销毁对象返回默认句柄（index = -1）。
        SlotHandle make_handle(const T* object) const noexcept
        {
            if (object == nullptr)
            {
                return SlotHandle{};
            }

            const Slot* slot = reinterpret_cast<const Slot*>(object);
            if (slot->index >= slots_.size() || slots_[slot->index].get() != slot || !slot->in_use)
            {
                return SlotHandle{};
            }

            return SlotHandle{slot->index, slot->generation};
        }

        // 由句柄取回对象：索引越界、槽位空闲或代际不符返回 nullptr。
        T* resolve(SlotHandle handle) const noexcept
        {
            if (handle.index >= slots_.size())
            {
                return nullptr;
            }

            const Slot& slot = *slots_[handle.index];
            if (!slot.in_use || slot.generation != handle.generation)
            {
                return nullptr;
            }

            return std::launder(reinterpret_cast<T*>(const_cast<unsigned char*>(slot.storage)));
        }

        // 句柄当前是否仍指向存活对象。
        bool alive(SlotHandle handle) const noexcept
        {
            return resolve(handle) != nullptr;
        }

        std::size_t alive() const noexcept
        {
            return alive_;
        }

        std::size_t capacity() const noexcept
        {
            return slots_.size();
        }

        bool owns(const T* object) const noexcept
        {
            if (object == nullptr)
            {
                return false;
            }
            const Slot* slot = reinterpret_cast<const Slot*>(object);
            return slot->index < slots_.size() && slots_[slot->index].get() == slot;
        }

    private:
        struct Slot
        {
            alignas(T) unsigned char storage[sizeof(T)];
            std::size_t index = 0;
            std::uint32_t generation = 0;
            bool in_use = false;
        };

        // 递增槽位代际。无符号回绕本身可接受，但这里跳过 0，
        // 避免 0 同时充当“初始代际”，保证相邻两次复用的代际一定不同。
        static void bump_generation(Slot& slot) noexcept
        {
            ++slot.generation;
            if (slot.generation == 0)
            {
                slot.generation = 1;
            }
        }

        std::vector<std::unique_ptr<Slot>> slots_;
        std::vector<std::size_t> free_;
        std::size_t alive_ = 0;
    };
}
