#include "TexelBuffer.h"

TexelBuffer::~TexelBuffer()
{
    release();
}

bool TexelBuffer::initialize(const float *data, std::size_t count)
{
    if (count == 0 || !data) {
        return false;
    }

    cachedData_.assign(data, data + count);
    return loadVolatile();
}

bool TexelBuffer::uploadGpuData(const float *data, std::size_t count)
{
    glGenBuffers(1, &buffer_);
    glBindBuffer(GL_TEXTURE_BUFFER, buffer_);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(count * sizeof(float)),
                 data,
                 GL_STATIC_DRAW);

    // Create the texture object that references the buffer.
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_BUFFER, texture_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, buffer_);

    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_BUFFER, 0);

    count_ = count;
    capacity_ = count;
    return true;
}

bool TexelBuffer::update(const float *data, std::size_t count)
{
    if (!data || count == 0) {
        return false;
    }

    cachedData_.assign(data, data + count);

    if (buffer_ == 0) {
        return loadVolatile();
    }

    glBindBuffer(GL_TEXTURE_BUFFER, buffer_);

    if (count > capacity_) {
        // Need to reallocate.
        glBufferData(GL_TEXTURE_BUFFER,
                     static_cast<GLsizeiptr>(count * sizeof(float)),
                     data,
                     GL_STATIC_DRAW);
        capacity_ = count;
    } else {
        // Update in-place.
        glBufferSubData(GL_TEXTURE_BUFFER, 0,
                        static_cast<GLsizeiptr>(count * sizeof(float)),
                        data);
    }

    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    count_ = count;
    return true;
}

void TexelBuffer::release()
{
    releaseGpuResources();
    cachedData_.clear();
    count_ = 0;
}

bool TexelBuffer::loadVolatile()
{
    if (cachedData_.empty()) {
        return false;
    }

    releaseGpuResources();
    return uploadGpuData(cachedData_.data(), cachedData_.size());
}

void TexelBuffer::unloadVolatile()
{
    releaseGpuResources();
}

void TexelBuffer::releaseGpuResources()
{
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    if (buffer_ != 0) {
        glDeleteBuffers(1, &buffer_);
        buffer_ = 0;
    }
    capacity_ = 0;
}
