#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

class GLVertexArray {
public:
    GLVertexArray();
    ~GLVertexArray();

    // Disable copy construction and copy assignment
    GLVertexArray(const GLVertexArray&) = delete;
    GLVertexArray& operator=(const GLVertexArray&) = delete;

    // Move constructor and move assignment
    GLVertexArray(GLVertexArray&& other) noexcept;
    GLVertexArray& operator=(GLVertexArray&& other) noexcept;

    void bind() const;
    void unbind() const;
    void setVertexData(const void* data, size_t size, GLenum usage = GL_STATIC_DRAW) const;
    void enableVertexAttribArray(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) const;
    void setIndexData(const void* data, size_t size, GLenum usage = GL_STATIC_DRAW) const;

    // Inline getter functions
    GLuint getVAO() const { return vaoID; }
    GLuint getVBO() const { return vboID; }
    GLuint getEBO() const { return eboID; }

private:
    GLuint vaoID = 0;
    GLuint vboID = 0;
    GLuint eboID = 0;
};

