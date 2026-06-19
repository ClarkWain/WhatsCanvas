#pragma once

#include <cstddef>
#include <vector>

#include <glad/glad.h>

/// A simple GPU-backed dynamic vertex buffer that grows as needed.
/// Avoids per-frame allocation by reusing a persistent GL buffer,
/// only reallocating when the required size exceeds current capacity.
class StreamBuffer
{
public:
    StreamBuffer() = default;
    ~StreamBuffer();

    StreamBuffer(const StreamBuffer &) = delete;
    StreamBuffer &operator=(const StreamBuffer &) = delete;

    /// Initialize the GL buffer with an initial capacity.
    void initialize(std::size_t initialCapacity = 4096);

    /// Release the GL buffer.
    void release();

    /// Upload vertex data to the GPU buffer, growing if necessary.
    /// Returns the GL buffer handle for binding.
    GLuint upload(const float *data, std::size_t floatCount);

    /// Get the current GL buffer handle (0 if not initialized).
    GLuint handle() const { return buffer_; }

    /// Get the current capacity in floats.
    std::size_t capacity() const { return capacity_; }

    /// Whether the buffer has been initialized.
    bool isInitialized() const { return buffer_ != 0; }

private:
    GLuint buffer_ = 0;
    std::size_t capacity_ = 0;
    static constexpr std::size_t GROW_FACTOR = 2;
};
