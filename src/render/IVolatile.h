#pragma once

#include <list>

/// Base interface for GPU resources that need lifecycle management.
/// When the OpenGL context is lost (e.g. mobile background/foreground
/// transitions, window recreation), all GPU resources become invalid.
/// Classes that hold GL objects should inherit from IVolatile and
/// implement loadVolatile()/unloadVolatile() to handle recovery.
///
/// All registered IVolatile instances are tracked in a global list.
/// Call unloadAll() before context destruction and loadAll() after
/// context recreation to restore all resources.
class IVolatile
{
public:
    IVolatile()
    {
        allInstances().push_back(this);
    }

    virtual ~IVolatile()
    {
        auto &instances = allInstances();
        instances.remove(this);
    }

    IVolatile(const IVolatile &) = delete;
    IVolatile &operator=(const IVolatile &) = delete;

    /// Recreate GPU resources. Called after context creation/recreation.
    /// Returns true on success.
    virtual bool loadVolatile() = 0;

    /// Release GPU resources. Called before context destruction.
    virtual void unloadVolatile() = 0;

    /// Unload all registered volatile resources.
    static void unloadAll()
    {
        for (auto *instance : allInstances()) {
            if (instance) {
                instance->unloadVolatile();
            }
        }
    }

    /// Load (recreate) all registered volatile resources.
    /// Returns the number of successfully loaded resources.
    static int loadAll()
    {
        int successCount = 0;
        for (auto *instance : allInstances()) {
            if (instance && instance->loadVolatile()) {
                ++successCount;
            }
        }
        return successCount;
    }

    /// Get the number of registered volatile resources.
    static std::size_t count()
    {
        return allInstances().size();
    }

private:
    static std::list<IVolatile *> &allInstances()
    {
        static std::list<IVolatile *> instances;
        return instances;
    }
};
