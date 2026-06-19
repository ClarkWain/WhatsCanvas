#include "StreamBuffer.h"

StreamBuffer::~StreamBuffer()
{
    release();
}

void StreamBuffer::initialize(std::size_t initialCapacity)
{
    if (buffer_ != 0) {
        return;
    }

    capacity_ = initialCapacity;
    glGenBuffers(1, &buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(capacity_ * sizeof(float)),
                 nullptr,
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void StreamBuffer::release()
{
    if (buffer_ != 0) {
        glDeleteBuffers(1, &buffer_);
        buffer_ = 0;
        capacity_ = 0;
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
