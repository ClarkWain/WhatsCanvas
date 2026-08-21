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

    capacityBytes_ =
        std::max<std::size_t>(1u, initialCapacity)
        * sizeof(float);
    glGenBuffers(1, &buffer_);
    allocateStorage(capacityBytes_);
    glBindBuffer(target_, 0);
}

void StreamBuffer::release()
{
    if (buffer_ != 0) {
        glDeleteBuffers(1, &buffer_);
        buffer_ = 0;
        capacityBytes_ = 0;
        writeOffsetBytes_ = 0;
    }
}

void StreamBuffer::abandon()
{
    buffer_ = 0;
    capacityBytes_ = 0;
    writeOffsetBytes_ = 0;
}

void StreamBuffer::beginFrame()
{
    writeOffsetBytes_ = 0;
    if (buffer_ != 0 && capacityBytes_ > 0) {
        allocateStorage(capacityBytes_);
    }
}

void StreamBuffer::reserveAdditionalBytes(std::size_t byteCount)
{
    if (byteCount == 0) {
        return;
    }
    if (buffer_ == 0) {
        const std::size_t initialFloatCapacity =
            std::max<std::size_t>(
                4096u,
                (byteCount + sizeof(float) - 1u)
                    / sizeof(float));
        initialize(initialFloatCapacity);
        return;
    }

    const std::size_t required =
        writeOffsetBytes_ + byteCount;
    if (required <= capacityBytes_) {
        return;
    }
    std::size_t nextCapacity = capacityBytes_;
    while (required > nextCapacity) {
        nextCapacity *= GROW_FACTOR;
    }
    capacityBytes_ = nextCapacity;
    allocateStorage(capacityBytes_);
    // Commands already submitted keep the orphaned backing store alive.
    writeOffsetBytes_ = 0;
}

GLuint StreamBuffer::upload(const float *data, std::size_t floatCount)
{
    if (buffer_ == 0) {
        initialize(floatCount);
    }

    // Grow buffer if needed.
    const std::size_t byteCount = floatCount * sizeof(float);
    if (byteCount > capacityBytes_) {
        while (byteCount > capacityBytes_) {
            capacityBytes_ *= GROW_FACTOR;
        }
        glBindBuffer(target_, buffer_);
        glBufferData(target_,
                     static_cast<GLsizeiptr>(capacityBytes_),
                     nullptr,
                     GL_DYNAMIC_DRAW);
    }

    glBindBuffer(target_, buffer_);
    glBufferSubData(target_, 0,
                    static_cast<GLsizeiptr>(byteCount),
                    data);
    return buffer_;
}

StreamBuffer::UploadRange StreamBuffer::uploadRange(
    const float *data, std::size_t floatCount)
{
    return uploadRangeBytes(
        data, floatCount * sizeof(float), alignof(float));
}

StreamBuffer::UploadRange StreamBuffer::uploadRange(
    const std::uint32_t *data, std::size_t indexCount)
{
    return uploadRangeBytes(
        data, indexCount * sizeof(std::uint32_t),
        alignof(std::uint32_t));
}

StreamBuffer::UploadRange StreamBuffer::uploadRange(
    const std::uint16_t *data, std::size_t indexCount)
{
    return uploadRangeBytes(
        data, indexCount * sizeof(std::uint16_t),
        alignof(std::uint16_t));
}

StreamBuffer::UploadRange StreamBuffer::uploadRange(
    const std::uint8_t *data, std::size_t byteCount)
{
    return uploadRangeBytes(
        data, byteCount, alignof(std::uint8_t));
}

StreamBuffer::UploadRange StreamBuffer::uploadBytes(
    const void *data, std::size_t byteCount,
    std::size_t alignment)
{
    return uploadRangeBytes(data, byteCount, alignment);
}

StreamBuffer::UploadRange StreamBuffer::uploadRangeBytes(
    const void *data, std::size_t byteCount,
    std::size_t alignment)
{
    if (byteCount == 0) {
        return {buffer_, 0};
    }
    if (buffer_ == 0) {
        const std::size_t initialFloatCapacity =
            std::max<std::size_t>(
                4096u,
                (byteCount + sizeof(float) - 1u)
                    / sizeof(float));
        initialize(initialFloatCapacity);
    }

    const auto alignedOffset =
        [alignment](std::size_t offset) {
            const std::size_t safeAlignment =
                std::max<std::size_t>(1u, alignment);
            return (offset + safeAlignment - 1u)
                / safeAlignment * safeAlignment;
        };
    std::size_t byteOffset =
        alignedOffset(writeOffsetBytes_);
    const std::size_t required =
        byteOffset + byteCount;
    if (required > capacityBytes_) {
        std::size_t nextCapacity = capacityBytes_;
        while (required > nextCapacity) {
            nextCapacity *= GROW_FACTOR;
        }
        capacityBytes_ = nextCapacity;
        allocateStorage(capacityBytes_);
        // The previous store remains alive for already queued draws.
        writeOffsetBytes_ = 0;
        byteOffset = 0;
    }

    glBindBuffer(target_, buffer_);
    glBufferSubData(
        target_, static_cast<GLintptr>(byteOffset),
        static_cast<GLsizeiptr>(byteCount), data);
    writeOffsetBytes_ = byteOffset + byteCount;
    return {buffer_, byteOffset};
}

void StreamBuffer::allocateStorage(std::size_t byteCapacity)
{
    glBindBuffer(target_, buffer_);
    glBufferData(
        target_,
        static_cast<GLsizeiptr>(byteCapacity),
        nullptr, GL_STREAM_DRAW);
}
