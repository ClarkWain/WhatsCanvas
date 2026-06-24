#include "RenderTargetPool.h"
#include "IRenderDevice.h"
#include "IRenderTarget.h"

std::unique_ptr<IRenderTarget> RenderTargetPool::acquire(int width, int height)
{
    // Search for a matching pooled target.
    for (auto it = pool_.begin(); it != pool_.end(); ++it) {
        if (it->width == width && it->height == height) {
            auto target = std::move(it->target);
            pool_.erase(it);
            return target;
        }
    }

    // No match found — create a new one.
    if (device_) {
        return device_->createRenderTarget(width, height);
    }
    return nullptr;
}

void RenderTargetPool::release(std::unique_ptr<IRenderTarget> target)
{
    if (!target || !target->isValid()) {
        return;
    }

    PooledTarget pooled;
    pooled.width = target->width();
    pooled.height = target->height();
    pooled.target = std::move(target);
    pooled.idleFrames = 0;
    pool_.push_back(std::move(pooled));
}

void RenderTargetPool::expire(int maxIdleFrames)
{
    // Age all pooled targets.
    for (auto &entry : pool_) {
        ++entry.idleFrames;
    }

    // Remove targets that have been idle too long.
    pool_.erase(
        std::remove_if(pool_.begin(), pool_.end(),
            [maxIdleFrames](const PooledTarget &entry) {
                return entry.idleFrames > maxIdleFrames;
            }),
        pool_.end());
}

void RenderTargetPool::clear()
{
    pool_.clear();
}
