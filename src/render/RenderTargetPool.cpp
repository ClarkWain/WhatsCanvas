#include "RenderTargetPool.h"
#include "IRenderDevice.h"
#include "IRenderTarget.h"

#include <limits>

std::size_t RenderTargetPool::estimateBytes(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h
        || w * h > std::numeric_limits<std::size_t>::max() / kEstimatedBytesPerPixel) {
        return std::numeric_limits<std::size_t>::max();
    }
    return w * h * kEstimatedBytesPerPixel;
}

std::unique_ptr<IRenderTarget> RenderTargetPool::acquire(int width, int height)
{
    // Search for a matching pooled target.
    for (auto it = pool_.begin(); it != pool_.end(); ++it) {
        const SharedImageResource resource =
            it->target ? it->target->getImageResource() : SharedImageResource();
        // getImageResource() creates the second reference here. Any additional
        // reference means a deferred composite/filter command still consumes
        // this texture, so reusing the target would overwrite live pixels.
        const bool externallyReferenced = resource && resource.use_count() > 2;
        if (it->width == width && it->height == height && !externallyReferenced) {
            auto target = std::move(it->target);
            pooledBytes_ -= it->estimatedBytes;
            pool_.erase(it);
            ++reuseCount_;
            return target;
        }
    }

    // No match found — create a new one.
    if (device_) {
        ++allocationCount_;
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
    pooled.idleCycles = 0;
    pooled.estimatedBytes = estimateBytes(pooled.width, pooled.height);

    if (maxPooledTargets_ == 0
        || pooled.estimatedBytes == 0
        || pooled.estimatedBytes > maxPooledBytes_) {
        ++evictionCount_;
        return;
    }

    while (!pool_.empty()
           && (pool_.size() >= maxPooledTargets_
               || pooledBytes_ > maxPooledBytes_ - pooled.estimatedBytes)) {
        evictOldest();
    }
    pooledBytes_ += pooled.estimatedBytes;
    pool_.push_back(std::move(pooled));
}

void RenderTargetPool::expire(int maxIdleCycles)
{
    // Age all pooled targets.
    for (auto &entry : pool_) {
        ++entry.idleCycles;
    }

    // Remove targets that have been idle too long.
    for (auto it = pool_.begin(); it != pool_.end();) {
        if (it->idleCycles > maxIdleCycles) {
            pooledBytes_ -= it->estimatedBytes;
            it = pool_.erase(it);
            ++evictionCount_;
        } else {
            ++it;
        }
    }
}

void RenderTargetPool::clear()
{
    pool_.clear();
    pooledBytes_ = 0;
}

void RenderTargetPool::evictOldest()
{
    if (pool_.empty()) {
        return;
    }
    pooledBytes_ -= pool_.front().estimatedBytes;
    pool_.erase(pool_.begin());
    ++evictionCount_;
}
