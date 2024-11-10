#include "GLVertexArray.h"
#include <iostream>

GLVertexArray::GLVertexArray() {
    glGenVertexArrays(1, &vaoID);
    glGenBuffers(1, &vboID);
    glGenBuffers(1, &eboID);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "构造函数中发生OpenGL错误: " << err << std::endl;
    }
}

GLVertexArray::~GLVertexArray() {
    glDeleteBuffers(1, &eboID);
    glDeleteBuffers(1, &vboID);
    glDeleteVertexArrays(1, &vaoID);
}

GLVertexArray::GLVertexArray(GLVertexArray&& other) noexcept
    : vaoID(other.vaoID), vboID(other.vboID), eboID(other.eboID) {
    other.vaoID = 0;
    other.vboID = 0;
    other.eboID = 0;
}

GLVertexArray& GLVertexArray::operator=(GLVertexArray&& other) noexcept {
    if (this != &other) {
        glDeleteBuffers(1, &eboID);
        glDeleteBuffers(1, &vboID);
        glDeleteVertexArrays(1, &vaoID);

        vaoID = other.vaoID;
        vboID = other.vboID;
        eboID = other.eboID;

        other.vaoID = 0;
        other.vboID = 0;
        other.eboID = 0;
    }
    return *this;
}

void GLVertexArray::bind() const {
    glBindVertexArray(vaoID);
}

void GLVertexArray::unbind() const {
    glBindVertexArray(0);
}

void GLVertexArray::setVertexData(const void* data, size_t size, GLenum usage) const {
    glBindBuffer(GL_ARRAY_BUFFER, vboID);
    glBufferData(GL_ARRAY_BUFFER, size, data, usage);
}

void GLVertexArray::enableVertexAttribArray(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) const {
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}

void GLVertexArray::setIndexData(const void* data, size_t size, GLenum usage) const {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eboID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, usage);
    
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "setIndexData中发生OpenGL错误: " << err << std::endl;
    }
}
