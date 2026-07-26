#include "SpriteBatch.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "opengl/GLProgram.h"
#include "opengl/GLShaderSource.h"
#include "render/RenderContext.h"
#include "render/RenderTypes.h"

SpriteBatch::SpriteBatch()
{
}

SpriteBatch::~SpriteBatch()
{
    releaseGLResources();
}

void SpriteBatch::setTexture(std::shared_ptr<ImageResource> texture)
{
    texture_ = std::move(texture);
}

void SpriteBatch::add(float x, float y, float width, float height,
                       float u0, float v0, float u1, float v1,
                       float r, float g, float b, float a,
                       const glm::mat4 &transform, float roundedRadius)
{
    // Apply transform to the 4 corner positions.
    const glm::vec4 tl = transform * glm::vec4(x, y, 0.0f, 1.0f);
    const glm::vec4 tr = transform * glm::vec4(x + width, y, 0.0f, 1.0f);
    const glm::vec4 br = transform * glm::vec4(x + width, y + height, 0.0f, 1.0f);
    const glm::vec4 bl = transform * glm::vec4(x, y + height, 0.0f, 1.0f);

    // Triangle 1: tl, tr, br
    vertexData_.insert(vertexData_.end(), {
        tl.x, tl.y, u0, v0, r, g, b, a,
        0.0f, 0.0f, roundedRadius, width, height});
    vertexData_.insert(vertexData_.end(), {
        tr.x, tr.y, u1, v0, r, g, b, a,
        1.0f, 0.0f, roundedRadius, width, height});
    vertexData_.insert(vertexData_.end(), {
        br.x, br.y, u1, v1, r, g, b, a,
        1.0f, 1.0f, roundedRadius, width, height});

    // Triangle 2: tl, br, bl
    vertexData_.insert(vertexData_.end(), {
        tl.x, tl.y, u0, v0, r, g, b, a,
        0.0f, 0.0f, roundedRadius, width, height});
    vertexData_.insert(vertexData_.end(), {
        br.x, br.y, u1, v1, r, g, b, a,
        1.0f, 1.0f, roundedRadius, width, height});
    vertexData_.insert(vertexData_.end(), {
        bl.x, bl.y, u0, v1, r, g, b, a,
        0.0f, 1.0f, roundedRadius, width, height});
}

void SpriteBatch::flush(RenderContext &context, DrawBlendMode blendMode)
{
    if (vertexData_.empty() || !texture_ || !texture_->isValid()) {
        return;
    }

    ensureGLInitialized();

    if (program_ == nullptr) {
        return;
    }

    // SpriteBatch currently batches only unclipped images. Apply state
    // explicitly instead of inheriting GL state left by the previous command.
    context.applyBlendMode(blendMode);
    context.applyClipState(ScissorState{}, ClipMaskState{});

    program_->use();
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()),
                                            static_cast<float>(context.getHeight()), 0.0f);
    program_->setMat4("uProjection", projection);
    program_->setInt("uTexture", 0);

    // Upload vertex data.
    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertexData_.size() * sizeof(float)),
                 vertexData_.data(),
                 GL_DYNAMIC_DRAW);

    // Bind texture.
    context.bindImageResource(texture_, DrawImageSampling::Linear, DrawImageTileMode::Clamp, false);

    // Draw all sprites in one call.
    glDrawArrays(
        GL_TRIANGLES, 0,
        static_cast<GLsizei>(vertexData_.size() / 13));

    glBindVertexArray(0);
}

void SpriteBatch::clear()
{
    vertexData_.clear();
}

void SpriteBatch::ensureGLInitialized()
{
    if (glInitialized_) {
        return;
    }

    const std::string vertexSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;
        layout (location = 2) in vec4 aColor;
        layout (location = 3) in vec2 aQuadUv;
        layout (location = 4) in vec3 aRounded;

        uniform mat4 uProjection;

        out vec2 vUv;
        out vec4 vColor;
        out vec2 vQuadUv;
        out vec3 vRounded;

        void main()
        {
            gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
            vUv = aUv;
            vColor = aColor;
            vQuadUv = aQuadUv;
            vRounded = aRounded;
        }
    )";

    const std::string fragmentSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        in vec2 vUv;
        in vec4 vColor;
        in vec2 vQuadUv;
        in vec3 vRounded;

        uniform sampler2D uTexture;

        out vec4 FragColor;

        float roundedRectCoverage()
        {
            float radius = vRounded.x;
            if (radius <= 0.0) {
                return 1.0;
            }
            vec2 size = vRounded.yz;
            vec2 halfSize = size * 0.5;
            radius = min(radius, min(halfSize.x, halfSize.y));
            vec2 point = vQuadUv * size;
            vec2 q = abs(point - halfSize) - (halfSize - vec2(radius));
            float distanceToEdge =
                length(max(q, vec2(0.0)))
                + min(max(q.x, q.y), 0.0) - radius;
            float aa = max(fwidth(distanceToEdge), 0.0001);
            return smoothstep(aa * 0.5, -aa * 0.5, distanceToEdge);
        }

        void main()
        {
            FragColor = texture(uTexture, vUv) * vColor;
            FragColor.a *= roundedRectCoverage();
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    glGenVertexArrays(1, &VAO_);
    glGenBuffers(1, &VBO_);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);

    // Position: 2 floats at offset 0.
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 13 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    // UV: 2 floats at offset 8.
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 13 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Color: 4 floats at offset 16.
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 13 * sizeof(float), (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(
        3, 2, GL_FLOAT, GL_FALSE, 13 * sizeof(float),
        (void *)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glVertexAttribPointer(
        4, 3, GL_FLOAT, GL_FALSE, 13 * sizeof(float),
        (void *)(10 * sizeof(float)));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);
    glInitialized_ = true;
}

void SpriteBatch::releaseGLResources()
{
    if (program_ != nullptr) {
        delete program_;
        program_ = nullptr;
    }

    if (VAO_ != static_cast<unsigned int>(-1)) {
        glDeleteVertexArrays(1, &VAO_);
        VAO_ = static_cast<unsigned int>(-1);
    }

    if (VBO_ != static_cast<unsigned int>(-1)) {
        glDeleteBuffers(1, &VBO_);
        VBO_ = static_cast<unsigned int>(-1);
    }

    glInitialized_ = false;
}
