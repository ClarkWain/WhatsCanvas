#include "StreamBuffer.h"

#include <algorithm>

StreamBuffer::~StreamBuffer()
{
    release();
}

void StreamBuffer::initialize(std::size_t initialCapacity)
{
    if (buffer_ != 0) {
        return;
    }

    capacity_ = std::max<std::size_t>(1u, initialCapacity);
    glGenBuffers(1, &buffer_);
    allocateStorage(capacity_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void StreamBuffer::release()
{
    if (buffer_ != 0) {
        glDeleteBuffers(1, &buffer_);
        buffer_ = 0;
        capacity_ = 0;
        writeOffset_ = 0;
    }
}

void StreamBuffer::beginFrame()
{
    writeOffset_ = 0;
    if (buffer_ != 0 && capacity_ > 0) {
        allocateStorage(capacity_);
    }
}

GLuint StreamBuffer::upload(const float *data, std::size_t floatCount)
{
    if (buffer_ == 0) {
        initialize(floatCount);
    }

    // Grow buffer if needed.
    if (floatCount > capacity_) {
        while (floatCount > capacity_) {
            capacity_ *= GROW_FACTOR;
        }
        glBindBuffer(GL_ARRAY_BUFFER, buffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(capacity_ * sizeof(float)),
                     nullptr,
                     GL_DYNAMIC_DRAW);
    }

    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(floatCount * sizeof(float)),
                    data);
    return buffer_;
}

StreamBuffer::UploadRange StreamBuffer::uploadRange(
    const float *data, std::size_t floatCount)
{
    return uploadRangeBytes(data, floatCount);
}

StreamBuffer::UploadRange StreamBuffer::uploadRange(
    const std::uint32_t *data, std::size_t indexCount)
{
    static_assert(
        sizeof(std::uint32_t) == sizeof(float),
        "StreamBuffer storage assumes 32-bit elements");
    return uploadRangeBytes(data, indexCount);
}

StreamBuffer::UploadRange StreamBuffer::uploadRangeBytes(
    const void *data, std::size_t elementCount)
{
    if (elementCount == 0) {
        return {buffer_, 0};
    }
    if (buffer_ == 0) {
        initialize(std::max<std::size_t>(4096u, elementCount));
    }

    const std::size_t required = writeOffset_ + elementCount;
    if (required > capacity_) {
        std::size_t nextCapacity = capacity_;
        while (required > nextCapacity) {
            nextCapacity *= GROW_FACTOR;
        }
        capacity_ = nextCapacity;
        allocateStorage(capacity_);
        // The previous store remains alive for already queued draws.
        writeOffset_ = 0;
    }

    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    const std::size_t byteOffset = writeOffset_ * sizeof(float);
    glBufferSubData(
        GL_ARRAY_BUFFER, static_cast<GLintptr>(byteOffset),
        static_cast<GLsizeiptr>(elementCount * sizeof(float)), data);
    writeOffset_ += elementCount;
    return {buffer_, byteOffset};
}

void StreamBuffer::allocateStorage(std::size_t capacity)
{
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(capacity * sizeof(float)),
        nullptr, GL_STREAM_DRAW);
}
