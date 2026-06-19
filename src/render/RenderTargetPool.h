#pragma once

#include <memory>
#include <vector>
#include <algorithm>

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
    explicit RenderTargetPool(const IRenderDevice *device)
        : device_(device)
    {
    }

    /// Acquire a render target with the given dimensions.
    /// Returns an existing one from the pool if available,
    /// or creates a new one if no match is found.
    std::unique_ptr<IRenderTarget> acquire(int width, int height);

    /// Release a render target back to the pool for reuse.
    /// The target is not destroyed; it can be acquired again later.
    void release(std::unique_ptr<IRenderTarget> target);

    /// Age all pooled targets and destroy those that have been
    /// unused for more than maxIdleFrames frames.
    void expire(int maxIdleFrames = 2);

    /// Get the number of targets currently in the pool.
    std::size_t pooledCount() const { return pool_.size(); }

    /// Clear all pooled targets (release GL resources).
    void clear();

private:
    struct PooledTarget
    {
        std::unique_ptr<IRenderTarget> target;
        int width = 0;
        int height = 0;
        int idleFrames = 0;
    };

    const IRenderDevice *device_ = nullptr;
    std::vector<PooledTarget> pool_;
};
