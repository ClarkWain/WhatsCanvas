#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glad/glad.h>

/// A simple GPU-backed dynamic vertex buffer that grows as needed.
/// Avoids per-frame allocation by reusing a persistent GL buffer,
/// only reallocating when the required size exceeds current capacity.
class StreamBuffer
{
public:
    struct UploadRange
    {
        GLuint buffer = 0;
        std::size_t byteOffset = 0;
    };

    StreamBuffer() = default;
    ~StreamBuffer();

    StreamBuffer(const StreamBuffer &) = delete;
    StreamBuffer &operator=(const StreamBuffer &) = delete;

    /// Initialize the GL buffer with an initial capacity.
    void initialize(std::size_t initialCapacity = 4096);

    /// Release the GL buffer.
    void release();

    /// Start a new frame using fresh backing storage.
    void beginFrame();

    /// Upload vertex data to the GPU buffer, growing if necessary.
    /// Returns the GL buffer handle for binding.
    GLuint upload(const float *data, std::size_t floatCount);

    /// Append vertex data and return its offset in the current frame stream.
    UploadRange uploadRange(const float *data, std::size_t floatCount);

    /// Append 32-bit index data. OpenGL buffer objects are untyped, so the
    /// caller may bind the returned buffer as GL_ELEMENT_ARRAY_BUFFER.
    UploadRange uploadRange(
        const std::uint32_t *data, std::size_t indexCount);
    UploadRange uploadRange(
        const std::uint16_t *data, std::size_t indexCount);

    /// Get the current GL buffer handle (0 if not initialized).
    GLuint handle() const { return buffer_; }

    /// Get the current capacity in floats.
    std::size_t capacity() const
    {
        return capacityBytes_ / sizeof(float);
    }

    /// Whether the buffer has been initialized.
    bool isInitialized() const { return buffer_ != 0; }

private:
    void allocateStorage(std::size_t byteCapacity);
    UploadRange uploadRangeBytes(
        const void *data, std::size_t byteCount,
        std::size_t alignment);

    GLuint buffer_ = 0;
    std::size_t capacityBytes_ = 0;
    std::size_t writeOffsetBytes_ = 0;
    static constexpr std::size_t GROW_FACTOR = 2;
};
