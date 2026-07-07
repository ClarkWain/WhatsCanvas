#include "GLProgram.h"
#include <utility>
#include "core/LogInternal.h"

// Move constructor
GLProgram::GLProgram(GLProgram &&other) noexcept
    : program_(other.program_),
      vertexSrc_(std::move(other.vertexSrc_)),
      fragmentSrc_(std::move(other.fragmentSrc_)),
      geometrySrc_(std::move(other.geometrySrc_))
{
    other.program_ = 0;
}

// Move assignment operator
GLProgram &GLProgram::operator=(GLProgram &&other) noexcept
{
    if (this != &other)
    {
        if (program_ != 0)
        {
            glDeleteProgram(program_);
        }
        program_ = other.program_;
        vertexSrc_ = std::move(other.vertexSrc_);
        fragmentSrc_ = std::move(other.fragmentSrc_);
        geometrySrc_ = std::move(other.geometrySrc_);
        other.program_ = 0;
    }
    return *this;
}

GLProgram::GLProgram(const std::string &vertexSrc, const std::string &fragmentSrc)
    : vertexSrc_(vertexSrc), fragmentSrc_(fragmentSrc)
{
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSrc);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    linkProgram(vertexShader, fragmentShader);
}

GLProgram::GLProgram(const std::string &vertexSrc, const std::string &geometrySrc, const std::string &fragmentSrc)
    : vertexSrc_(vertexSrc), fragmentSrc_(fragmentSrc), geometrySrc_(geometrySrc)
{
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSrc);
    GLuint geometryShader = compileShader(GL_GEOMETRY_SHADER, geometrySrc);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    linkProgram(vertexShader, fragmentShader, geometryShader);
}

bool GLProgram::loadVolatile()
{
    if (program_ != 0) {
        return true;  // Already loaded.
    }
    if (vertexSrc_.empty() || fragmentSrc_.empty()) {
        return false;  // No source to recompile from.
    }

    try {
        GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSrc_);
        GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSrc_);
        if (!geometrySrc_.empty()) {
            GLuint geometryShader = compileShader(GL_GEOMETRY_SHADER, geometrySrc_);
            linkProgram(vertexShader, fragmentShader, geometryShader);
        } else {
            linkProgram(vertexShader, fragmentShader);
        }
        return true;
    } catch (const GLProgramException &) {
        return false;
    }
}

void GLProgram::unloadVolatile()
{
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

GLProgram::~GLProgram()
{
    glDeleteProgram(program_);
}

void GLProgram::linkProgram(GLuint vertexShader, GLuint fragmentShader, GLuint geometryShader)
{
    program_ = glCreateProgram();
    glAttachShader(program_, vertexShader);
    glAttachShader(program_, fragmentShader);

    if (geometryShader != 0)
    {
        glAttachShader(program_, geometryShader);
    }

    glLinkProgram(program_);
    checkLinkErrors();

    // Delete shader objects
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (geometryShader != 0)
    {
        glDeleteShader(geometryShader);
    }
}

void GLProgram::checkCompileErrors(GLuint shader, const std::string &type)
{
    GLint success;
    char infoLog[1024];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success)
    {
        glGetShaderInfoLog(shader, 1024, NULL, infoLog);
        WSC_LOG_ERROR("GLProgram", "Shader compilation error of type " << type << ": " << infoLog);
    }
}

void GLProgram::checkLinkErrors()
{
    GLint success;
    char infoLog[1024];
    glGetProgramiv(program_, GL_LINK_STATUS, &success);

    if (!success)
    {
        glGetProgramInfoLog(program_, 1024, NULL, infoLog);
        WSC_LOG_ERROR("GLProgram", "Shader program linking error: " << infoLog);
    }
}

void GLProgram::use()
{
    glUseProgram(program_);
}

GLuint GLProgram::getProgram() const
{
    return program_;
}

GLuint GLProgram::compileShader(GLenum type, const std::string &source)
{
    GLuint shader = glCreateShader(type);
    const char *src = source.c_str();
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        WSC_LOG_ERROR("GLProgram", "Shader compilation failed: " << infoLog);
    }

    return shader;
}

void GLProgram::setFloat(const std::string &name, float value)
{
    glUniform1f(glGetUniformLocation(program_, name.c_str()), value);
}

void GLProgram::setInt(const std::string &name, int value)
{
    glUniform1i(glGetUniformLocation(program_, name.c_str()), value);
}

void GLProgram::setVec2(const std::string &name, const glm::vec2 &value)
{
    glUniform2fv(glGetUniformLocation(program_, name.c_str()), 1, &value[0]);
}

void GLProgram::setVec3(const std::string &name, const glm::vec3 &value)
{
    glUniform3fv(glGetUniformLocation(program_, name.c_str()), 1, &value[0]);
}

void GLProgram::setVec4(const std::string &name, const glm::vec4 &value)
{
    glUniform4fv(glGetUniformLocation(program_, name.c_str()), 1, &value[0]);
}

void GLProgram::setMat4(const std::string &name, const glm::mat4 &value)
{
    glUniformMatrix4fv(glGetUniformLocation(program_, name.c_str()), 1, GL_FALSE, &value[0][0]);
}
