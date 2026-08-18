#include "GLProgram.h"
#include <atomic>
#include <chrono>
#include <utility>
#include "core/LogInternal.h"

namespace {

std::atomic<std::size_t> g_programLinkCount{0};
std::atomic<std::size_t> g_shaderCompileCount{0};
std::atomic<std::uint64_t> g_shaderCompileCpuTimeNs{0};
std::atomic<std::uint64_t> g_programLinkCpuTimeNs{0};

std::uint64_t elapsedNanoseconds(
    const std::chrono::steady_clock::time_point start)
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count());
}

} // namespace

// Move constructor
GLProgram::GLProgram(GLProgram &&other) noexcept
    : program_(other.program_),
      vertexSrc_(std::move(other.vertexSrc_)),
      fragmentSrc_(std::move(other.fragmentSrc_)),
      geometrySrc_(std::move(other.geometrySrc_)),
      debugLabel_(std::move(other.debugLabel_)),
      currentCompileCpuTimeNs_(other.currentCompileCpuTimeNs_),
      uniformLocations_(std::move(other.uniformLocations_))
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
        debugLabel_ = std::move(other.debugLabel_);
        currentCompileCpuTimeNs_ = other.currentCompileCpuTimeNs_;
        uniformLocations_ = std::move(other.uniformLocations_);
        other.program_ = 0;
    }
    return *this;
}

GLProgram::GLProgram(const std::string &vertexSrc, const std::string &fragmentSrc)
    : GLProgram("unnamed", vertexSrc, fragmentSrc)
{
}

GLProgram::GLProgram(
    const char *debugLabel, const std::string &vertexSrc,
    const std::string &fragmentSrc)
    : vertexSrc_(vertexSrc), fragmentSrc_(fragmentSrc)
      , debugLabel_(debugLabel != nullptr ? debugLabel : "unnamed")
{
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSrc);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    linkProgram(vertexShader, fragmentShader);
}

GLProgram::GLProgram(const std::string &vertexSrc, const std::string &geometrySrc, const std::string &fragmentSrc)
    : vertexSrc_(vertexSrc), fragmentSrc_(fragmentSrc), geometrySrc_(geometrySrc),
      debugLabel_("unnamed_geometry")
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
        currentCompileCpuTimeNs_ = 0;
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
    uniformLocations_.clear();
}

void GLProgram::abandonVolatile()
{
    program_ = 0;
    uniformLocations_.clear();
}

GLProgram::~GLProgram()
{
    glDeleteProgram(program_);
}

void GLProgram::linkProgram(GLuint vertexShader, GLuint fragmentShader, GLuint geometryShader)
{
    uniformLocations_.clear();
    program_ = glCreateProgram();
    glAttachShader(program_, vertexShader);
    glAttachShader(program_, fragmentShader);

    if (geometryShader != 0)
    {
        glAttachShader(program_, geometryShader);
    }

    const auto linkStart = std::chrono::steady_clock::now();
    glLinkProgram(program_);
    checkLinkErrors();
    const std::uint64_t linkTimeNs = elapsedNanoseconds(linkStart);
    g_programLinkCpuTimeNs.fetch_add(
        linkTimeNs, std::memory_order_relaxed);
    g_programLinkCount.fetch_add(1u, std::memory_order_relaxed);
    WSC_LOG_INFO(
        "GLProgram",
        debugLabel_ << " stages=" << (geometryShader != 0 ? 3 : 2)
                    << " compileUs=" << currentCompileCpuTimeNs_ / 1000u
                    << " linkUs=" << linkTimeNs / 1000u);

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

GLProgramCompilationStats GLProgram::compilationStats()
{
    GLProgramCompilationStats stats;
    stats.programLinkCount =
        g_programLinkCount.load(std::memory_order_relaxed);
    stats.shaderCompileCount =
        g_shaderCompileCount.load(std::memory_order_relaxed);
    stats.shaderCompileCpuTimeNs =
        g_shaderCompileCpuTimeNs.load(std::memory_order_relaxed);
    stats.programLinkCpuTimeNs =
        g_programLinkCpuTimeNs.load(std::memory_order_relaxed);
    return stats;
}

GLuint GLProgram::compileShader(GLenum type, const std::string &source)
{
    GLuint shader = glCreateShader(type);
    const char *src = source.c_str();
    glShaderSource(shader, 1, &src, NULL);
    const auto compileStart = std::chrono::steady_clock::now();
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    const std::uint64_t compileTimeNs = elapsedNanoseconds(compileStart);
    currentCompileCpuTimeNs_ += compileTimeNs;
    g_shaderCompileCpuTimeNs.fetch_add(
        compileTimeNs, std::memory_order_relaxed);
    g_shaderCompileCount.fetch_add(1u, std::memory_order_relaxed);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        WSC_LOG_ERROR("GLProgram", "Shader compilation failed: " << infoLog);
    }

    return shader;
}

GLint GLProgram::uniformLocation(const std::string &name)
{
    const auto found = uniformLocations_.find(name);
    if (found != uniformLocations_.end()) {
        return found->second;
    }
    const GLint location = glGetUniformLocation(program_, name.c_str());
    uniformLocations_.emplace(name, location);
    return location;
}

void GLProgram::setFloat(const std::string &name, float value)
{
    glUniform1f(uniformLocation(name), value);
}

void GLProgram::setInt(const std::string &name, int value)
{
    glUniform1i(uniformLocation(name), value);
}

void GLProgram::setVec2(const std::string &name, const glm::vec2 &value)
{
    glUniform2fv(uniformLocation(name), 1, &value[0]);
}

void GLProgram::setVec3(const std::string &name, const glm::vec3 &value)
{
    glUniform3fv(uniformLocation(name), 1, &value[0]);
}

void GLProgram::setVec4(const std::string &name, const glm::vec4 &value)
{
    glUniform4fv(uniformLocation(name), 1, &value[0]);
}

void GLProgram::setMat4(const std::string &name, const glm::mat4 &value)
{
    glUniformMatrix4fv(
        uniformLocation(name), 1, GL_FALSE, &value[0][0]);
}
