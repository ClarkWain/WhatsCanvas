#pragma once

#include <functional>

/// Handles window resize events by notifying all registered listeners
/// and providing utilities for automatic viewport/resource updates.
class ResizeHandler
{
public:
    using ResizeCallback = std::function<void(int width, int height)>;

    /// Get the singleton instance.
    static ResizeHandler &instance()
    {
        static ResizeHandler handler;
        return handler;
    }

    /// Register a callback to be invoked on window resize.
    /// Returns an ID that can be used to unregister.
    int addListener(ResizeCallback callback)
    {
        int id = nextId_++;
        listeners_.push_back({id, std::move(callback)});
        return id;
    }

    /// Remove a listener by ID.
    void removeListener(int id)
    {
        listeners_.erase(
            std::remove_if(listeners_.begin(), listeners_.end(),
                [id](const Listener &l) { return l.id == id; }),
            listeners_.end());
    }

    /// Notify all listeners of a resize event.
    void notify(int width, int height)
    {
        if (width <= 0 || height <= 0) {
            return;
        }

        currentWidth_ = width;
        currentHeight_ = height;

        for (const auto &listener : listeners_) {
            if (listener.callback) {
                listener.callback(width, height);
            }
        }
    }

    /// Get the current window dimensions.
    int currentWidth() const { return currentWidth_; }
    int currentHeight() const { return currentHeight_; }

    /// Get the aspect ratio (width / height).
    float aspectRatio() const
    {
        return currentHeight_ > 0 ? static_cast<float>(currentWidth_) / static_cast<float>(currentHeight_) : 1.0f;
    }

private:
    ResizeHandler() = default;

    struct Listener
    {
        int id;
        ResizeCallback callback;
    };

    std::vector<Listener> listeners_;
    int nextId_ = 1;
    int currentWidth_ = 0;
    int currentHeight_ = 0;
};
