#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <glad/glad.h>

/// An asynchronous pixel readback request.
/// Submits a glReadPixels to a PBO (Pixel Buffer Object) without
/// blocking the CPU. The result is available after the GL fence
/// signals completion, typically on the next frame.
class AsyncReadback
{
public:
    using Callback = std::function<void(std::vector<unsigned char> pixels, int width, int height)>;

    AsyncReadback() = default;
    ~AsyncReadback();

    AsyncReadback(const AsyncReadback &) = delete;
    AsyncReadback &operator=(const AsyncReadback &) = delete;

    /// Submit an async readback request for the current framebuffer.
    /// The callback will be invoked when the data is ready.
    bool submit(int x, int y, int width, int height, Callback callback);

    /// Check if the readback is complete and invoke the callback if so.
    /// Returns true if the readback completed (or was never submitted).
    bool checkCompletion();

    /// Whether a readback request is pending.
    bool isPending() const { return pending_; }

private:
    bool pending_ = false;
    GLuint pbo_ = 0;
    GLsync fence_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    Callback callback_;

    void cleanup();
};
