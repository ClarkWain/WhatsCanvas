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

    release();

    // Create the buffer object.
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
    return true;
}

bool TexelBuffer::update(const float *data, std::size_t count)
{
    if (!data || count == 0) {
        return false;
    }

    if (buffer_ == 0) {
        return initialize(data, count);
    }

    glBindBuffer(GL_TEXTURE_BUFFER, buffer_);

    if (count > count_) {
        // Need to reallocate.
        glBufferData(GL_TEXTURE_BUFFER,
                     static_cast<GLsizeiptr>(count * sizeof(float)),
                     data,
                     GL_STATIC_DRAW);
        count_ = count;
    } else {
        // Update in-place.
        glBufferSubData(GL_TEXTURE_BUFFER, 0,
                        static_cast<GLsizeiptr>(count * sizeof(float)),
                        data);
    }

    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    return true;
}

void TexelBuffer::release()
{
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    if (buffer_ != 0) {
        glDeleteBuffers(1, &buffer_);
        buffer_ = 0;
    }
    count_ = 0;
}
