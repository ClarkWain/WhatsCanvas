#include "ClipCoverageProgram.h"

#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "GLShaderSource.h"
#include "GLProgram.h"

namespace wsc::opengl {

ClipCoverageProgram *ClipCoverageProgram::instance_ = nullptr;

ClipCoverageProgram::~ClipCoverageProgram()
{
    release();
}

void ClipCoverageProgram::initialize()
{
    if (initialized_) {
        return;
    }

    const std::string coverageVert = std::string(shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in float aCoverage;
        uniform mat4 uProjection;
        uniform mat4 uTransform;
        out float vCoverage;
        void main()
        {
            vCoverage = aCoverage;
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
        }
    )";

    const std::string coverageFrag = std::string(shaderVersionDirective()) + R"(
        in float vCoverage;
        out vec4 FragColor;
        void main()
        {
            float c = clamp(vCoverage, 0.0, 1.0);
            FragColor = vec4(c, c, c, c);
        }
    )";

    const std::string multiplyVert = std::string(shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;
        out vec2 vUv;
        void main()
        {
            vUv = aUv;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    const std::string multiplyFrag = std::string(shaderVersionDirective()) + R"(
        in vec2 vUv;
        uniform sampler2D uTexture;
        out vec4 FragColor;
        void main()
        {
            float r = texture(uTexture, vUv).r;
            FragColor = vec4(r, r, r, r);
        }
    )";

    coverageProgram_ = new GLProgram(coverageVert, coverageFrag);
    multiplyProgram_ = new GLProgram(multiplyVert, multiplyFrag);

    glGenVertexArrays(1, &coverageVao_);
    glGenBuffers(1, &coverageVbo_);
    glBindVertexArray(coverageVao_);
    glBindBuffer(GL_ARRAY_BUFFER, coverageVbo_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    const float quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };
    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    initialized_ = true;
}

void ClipCoverageProgram::destroyTargets()
{
    if (accumulatorFbo_ != 0) {
        glDeleteFramebuffers(1, &accumulatorFbo_);
        accumulatorFbo_ = 0;
    }
    if (tempFbo_ != 0) {
        glDeleteFramebuffers(1, &tempFbo_);
        tempFbo_ = 0;
    }
    if (accumulatorTexture_ != 0) {
        glDeleteTextures(1, &accumulatorTexture_);
        accumulatorTexture_ = 0;
    }
    if (tempTexture_ != 0) {
        glDeleteTextures(1, &tempTexture_);
        tempTexture_ = 0;
    }
    targetWidth_ = 0;
    targetHeight_ = 0;
}

void ClipCoverageProgram::release()
{
    if (!initialized_) {
        return;
    }
    delete coverageProgram_;
    coverageProgram_ = nullptr;
    delete multiplyProgram_;
    multiplyProgram_ = nullptr;
    if (coverageVbo_ != 0) {
        glDeleteBuffers(1, &coverageVbo_);
        coverageVbo_ = 0;
    }
    if (coverageVao_ != 0) {
        glDeleteVertexArrays(1, &coverageVao_);
        coverageVao_ = 0;
    }
    if (quadVbo_ != 0) {
        glDeleteBuffers(1, &quadVbo_);
        quadVbo_ = 0;
    }
    if (quadVao_ != 0) {
        glDeleteVertexArrays(1, &quadVao_);
        quadVao_ = 0;
    }
    coverageVboCapacity_ = 0;
    destroyTargets();
    initialized_ = false;
}

namespace {

GLuint createR8Target(int width, int height, GLuint &framebuffer)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!complete) {
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &texture);
        framebuffer = 0;
        return 0;
    }
    return texture;
}

} // namespace

bool ClipCoverageProgram::ensureTargets(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (accumulatorFbo_ != 0 && tempFbo_ != 0 && targetWidth_ == width && targetHeight_ == height) {
        return true;
    }
    destroyTargets();
    accumulatorTexture_ = createR8Target(width, height, accumulatorFbo_);
    tempTexture_ = createR8Target(width, height, tempFbo_);
    if (accumulatorTexture_ == 0 || tempTexture_ == 0) {
        destroyTargets();
        return false;
    }
    targetWidth_ = width;
    targetHeight_ = height;
    return true;
}

void ClipCoverageProgram::beginAccumulator(int width, int height)
{
    glBindFramebuffer(GL_FRAMEBUFFER, accumulatorFbo_);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void ClipCoverageProgram::beginClipLayer(int width, int height)
{
    glBindFramebuffer(GL_FRAMEBUFFER, tempFbo_);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // Keep the maximum coverage where interior and fringe triangles overlap.
    glEnable(GL_BLEND);
    glBlendEquation(GL_MAX);
    glBlendFunc(GL_ONE, GL_ONE);
}

void ClipCoverageProgram::ensureCoverageBuffer(std::size_t floatCount)
{
    const std::size_t bytes = floatCount * sizeof(float);
    glBindBuffer(GL_ARRAY_BUFFER, coverageVbo_);
    if (bytes > coverageVboCapacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_DYNAMIC_DRAW);
        coverageVboCapacity_ = bytes;
    }
}

void ClipCoverageProgram::drawCoverage(const std::vector<float> &points, const std::vector<float> &coverage,
                                       const glm::mat4 &transform, int width, int height)
{
    const std::size_t vertexCount = points.size() / 2;
    if (vertexCount < 3 || coverage.size() < vertexCount) {
        return;
    }

    std::vector<float> interleaved;
    interleaved.reserve(vertexCount * 3);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        interleaved.push_back(points[i * 2 + 0]);
        interleaved.push_back(points[i * 2 + 1]);
        interleaved.push_back(coverage[i]);
    }

    coverageProgram_->use();
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(width),
                                            static_cast<float>(height), 0.0f);
    coverageProgram_->setMat4("uProjection", projection);
    coverageProgram_->setMat4("uTransform", transform);

    glBindVertexArray(coverageVao_);
    ensureCoverageBuffer(interleaved.size());
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(interleaved.size() * sizeof(float)),
                    interleaved.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCount));
    glBindVertexArray(0);
}

void ClipCoverageProgram::multiplyLayerIntoAccumulator(int width, int height)
{
    glBindFramebuffer(GL_FRAMEBUFFER, accumulatorFbo_);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR); // accumulator *= temp
    multiplyProgram_->use();
    multiplyProgram_->setInt("uTexture", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tempTexture_);
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendEquation(GL_FUNC_ADD);
}

} // namespace wsc::opengl
