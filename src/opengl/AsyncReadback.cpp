#include "AsyncReadback.h"

#include <cstring>
#include <iostream>
#include "core/LogInternal.h"

AsyncReadback::~AsyncReadback()
{
    cleanup();
}

bool AsyncReadback::submit(int x, int y, int width, int height, Callback callback)
{
    if (pending_) {
        // A readback is already in progress — wait for it first.
        checkCompletion();
        if (pending_) {
            WSC_LOG_WARN("AsyncReadback", "Previous readback still pending, skipping new request.");
            return false;
        }
    }

    if (width <= 0 || height <= 0) {
        return false;
    }

    width_ = width;
    height_ = height;
    callback_ = std::move(callback);

    const std::size_t bufferSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;

    // Create PBO if needed.
    if (pbo_ == 0) {
        glGenBuffers(1, &pbo_);
    }

    // Bind PBO and initiate async readback.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_);
    glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bufferSize), nullptr, GL_STREAM_READ);

    // Set pixel store alignment.
    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    // Insert a fence sync object.
    if (fence_ != nullptr) {
        glDeleteSync(fence_);
    }
    fence_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    pending_ = true;
    return true;
}

bool AsyncReadback::checkCompletion()
{
    if (!pending_ || fence_ == nullptr) {
        return true;
    }

    // Check fence status without blocking.
    GLint status = 0;
    glGetSynciv(fence_, GL_SYNC_STATUS, sizeof(status), nullptr, &status);

    if (status != GL_SIGNALED) {
        return false;  // Not ready yet.
    }

    // Fence signaled — read data from PBO.
    const std::size_t bufferSize = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4;

    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_);
    const void *mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(bufferSize), GL_MAP_READ_BIT);

    if (mapped) {
        std::vector<unsigned char> pixels(bufferSize);

        // Flip vertically (OpenGL reads bottom-up).
        const std::size_t rowSize = static_cast<std::size_t>(width_) * 4;
        const auto *src = static_cast<const unsigned char *>(mapped);
        for (int y = 0; y < height_; ++y) {
            const std::size_t srcOffset = static_cast<std::size_t>(height_ - 1 - y) * rowSize;
            const std::size_t dstOffset = static_cast<std::size_t>(y) * rowSize;
            std::copy(src + srcOffset, src + srcOffset + rowSize, pixels.begin() + dstOffset);
        }

        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

        if (callback_) {
            callback_(std::move(pixels), width_, height_);
        }
    } else {
        WSC_LOG_ERROR("AsyncReadback", "Failed to map PBO for readback.");
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    cleanup();
    return true;
}

void AsyncReadback::cleanup()
{
    if (fence_ != nullptr) {
        glDeleteSync(fence_);
        fence_ = nullptr;
    }
    if (pbo_ != 0) {
        glDeleteBuffers(1, &pbo_);
        pbo_ = 0;
    }
    pending_ = false;
    callback_ = nullptr;
}
