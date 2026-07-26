#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "IRenderTarget.h"

class IRenderTarget;
class IRenderDevice;

/// A pool of reusable render targets to avoid frequent GL
/// object allocation/deallocation during offscreen rendering.
///
/// When a render target with matching dimensions is requested,
/// the pool returns an existing one instead of creating a new one.
/// Unused targets are cleaned up after N frames of inactivity.
class RenderTargetPool
{
public:
    static constexpr std::size_t kDefaultMaxPooledBytes = 32u * 1024u * 1024u;
    static constexpr std::size_t kDefaultMaxPooledTargets = 12u;
    static constexpr std::size_t kEstimatedBytesPerPixel = 8u;
    static constexpr int kDefaultMaxIdleCycles = 64;

    explicit RenderTargetPool(
        const IRenderDevice *device,
        std::size_t maxPooledBytes = kDefaultMaxPooledBytes,
        std::size_t maxPooledTargets = kDefaultMaxPooledTargets)
        : device_(device),
          maxPooledBytes_(maxPooledBytes),
          maxPooledTargets_(maxPooledTargets)
    {
    }

    /// Acquire a render target with the given dimensions.
    /// Returns an existing one from the pool if available,
    /// or creates a new one if no match is found.
    std::unique_ptr<IRenderTarget> acquire(int width, int height);

    /// Release a render target back to the pool for reuse.
    /// The target is not destroyed; it can be acquired again later.
    void release(std::unique_ptr<IRenderTarget> target);

    /// Age all pooled targets and destroy those that have been unused for more
    /// than maxIdleCycles acquire/release cycles.
    void expire(int maxIdleCycles = kDefaultMaxIdleCycles);

    /// Get the number of targets currently in the pool.
    std::size_t pooledCount() const { return pool_.size(); }
    std::size_t pooledBytes() const { return pooledBytes_; }
    std::size_t maxPooledBytes() const { return maxPooledBytes_; }
    std::size_t reuseCount() const { return reuseCount_; }
    std::size_t allocationCount() const { return allocationCount_; }
    std::size_t evictionCount() const { return evictionCount_; }

    /// Clear all pooled targets and release their backend resources.
    void clear();

private:
    static std::size_t estimateBytes(int width, int height);
    void evictOldest();

    struct PooledTarget
    {
        std::unique_ptr<IRenderTarget> target;
        int width = 0;
        int height = 0;
        int idleCycles = 0;
        std::size_t estimatedBytes = 0;
    };

    const IRenderDevice *device_ = nullptr;
    std::size_t maxPooledBytes_ = kDefaultMaxPooledBytes;
    std::size_t maxPooledTargets_ = kDefaultMaxPooledTargets;
    std::size_t pooledBytes_ = 0;
    std::size_t reuseCount_ = 0;
    std::size_t allocationCount_ = 0;
    std::size_t evictionCount_ = 0;
    std::vector<PooledTarget> pool_;
};
