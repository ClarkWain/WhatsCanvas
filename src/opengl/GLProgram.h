#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <stdexcept>
#include <glm.hpp>

// 自定义异常类
class GLProgramException : public std::runtime_error {
public:
    explicit GLProgramException(const std::string& message) : std::runtime_error(message) {}
};

class GLProgram {
public:
    GLProgram(const std::string& vertexSrc, const std::string& fragmentSrc);
    GLProgram(const std::string& vertexSrc, const std::string& geometrySrc, const std::string& fragmentSrc);
    // 禁用拷贝
    GLProgram(const GLProgram&) = delete;
    GLProgram& operator=(const GLProgram&) = delete;
    // 启用移动
    GLProgram(GLProgram&& other) noexcept;
    GLProgram& operator=(GLProgram&& other) noexcept;
    
    ~GLProgram();
    void use();
    GLuint getProgram() const;

    // Uniform设置方法
    void setFloat(const std::string& name, float value);
    void setInt(const std::string& name, int value);
    void setVec2(const std::string& name, const glm::vec2& value);
    void setVec3(const std::string& name, const glm::vec3& value);
    void setVec4(const std::string& name, const glm::vec4& value);
    void setMat4(const std::string& name, const glm::mat4& value);

private:
    GLuint program_;
    GLuint compileShader(GLenum type, const std::string& source);
    void linkProgram(GLuint vertexShader, GLuint fragmentShader, GLuint geometryShader = 0);
    void checkCompileErrors(GLuint shader, const std::string& type);
    void checkLinkErrors();
};
