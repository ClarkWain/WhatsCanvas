#pragma once

#include <cstddef>
#include <new>

namespace wsc::detail {

struct DrawPathCommandPool {
    void *freeList = nullptr;
    std::size_t retainedCount = 0;

    ~DrawPathCommandPool()
    {
        while (freeList != nullptr) {
            void *block = freeList;
            freeList = *static_cast<void **>(block);
            ::operator delete(block);
        }
    }
};

inline thread_local DrawPathCommandPool drawPathCommandPool;

inline void *allocateDrawPathCommand(std::size_t size)
{
    if (drawPathCommandPool.freeList == nullptr) {
        return ::operator new(size);
    }
    void *memory = drawPathCommandPool.freeList;
    drawPathCommandPool.freeList =
        *static_cast<void **>(memory);
    --drawPathCommandPool.retainedCount;
    return memory;
}

inline void releaseDrawPathCommand(void *memory) noexcept
{
    if (memory == nullptr) {
        return;
    }
    constexpr std::size_t kMaximumRetainedCommands = 8192u;
    if (drawPathCommandPool.retainedCount
        >= kMaximumRetainedCommands) {
        ::operator delete(memory);
        return;
    }
    *static_cast<void **>(memory) =
        drawPathCommandPool.freeList;
    drawPathCommandPool.freeList = memory;
    ++drawPathCommandPool.retainedCount;
}

} // namespace wsc::detail
