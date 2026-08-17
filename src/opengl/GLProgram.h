#pragma once

#include <glad/glad.h>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <glm/glm.hpp>
#include "render/IVolatile.h"

// Custom exception type
class GLProgramException : public std::runtime_error {
public:
    explicit GLProgramException(const std::string& message) : std::runtime_error(message) {}
};

class GLProgram : public IVolatile {
public:
    GLProgram(const std::string& vertexSrc, const std::string& fragmentSrc);
    GLProgram(const std::string& vertexSrc, const std::string& geometrySrc, const std::string& fragmentSrc);
    // Disable copy operations
    GLProgram(const GLProgram&) = delete;
    GLProgram& operator=(const GLProgram&) = delete;
    // Enable move operations
    GLProgram(GLProgram&& other) noexcept;
    GLProgram& operator=(GLProgram&& other) noexcept;
    
    ~GLProgram() override;
    void use();
    GLuint getProgram() const;

    // IVolatile interface
    bool loadVolatile() override;
    void unloadVolatile() override;
    /// Forget the program name after context loss without calling glDeleteProgram.
    void abandonVolatile();

    // Uniform setter helpers
    void setFloat(const std::string& name, float value);
    void setInt(const std::string& name, int value);
    void setVec2(const std::string& name, const glm::vec2& value);
    void setVec3(const std::string& name, const glm::vec3& value);
    void setVec4(const std::string& name, const glm::vec4& value);
    void setMat4(const std::string& name, const glm::mat4& value);

private:
    GLuint program_;
    std::string vertexSrc_;
    std::string fragmentSrc_;
    std::string geometrySrc_;
    std::unordered_map<std::string, GLint> uniformLocations_;
    GLuint compileShader(GLenum type, const std::string& source);
    GLint uniformLocation(const std::string& name);
    void linkProgram(GLuint vertexShader, GLuint fragmentShader, GLuint geometryShader = 0);
    void checkCompileErrors(GLuint shader, const std::string& type);
    void checkLinkErrors();
};
