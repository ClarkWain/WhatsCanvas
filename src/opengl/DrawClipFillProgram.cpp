#include "opengl/DrawClipFillProgram.h"

#include <string>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "opengl/GLProgram.h"
#include "opengl/GLShaderSource.h"
#include "opengl/GlobalIndexBuffers.h"
#include "render/RenderContext.h"

namespace wsc::opengl {

DrawClipFillProgram *DrawClipFillProgram::instance_ = nullptr;

DrawClipFillProgram::~DrawClipFillProgram()
{
    release();
}

void DrawClipFillProgram::initialize()
{
    if (initialized_) {
        return;
    }

    const std::string vertexSrc = std::string(shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;   // NDC directly
        layout (location = 1) in vec4 aColor; // per-vertex tint
        layout (location = 2) in vec2 aUv;    // 0..1 mask UV

        out vec4 vColor;
        out vec2 vUv;

        void main()
        {
            gl_Position = vec4(aPos, 0.0, 1.0);
            vColor = aColor;
            vUv = aUv;
        }
    )";

    const std::string fragmentSrc = std::string(shaderVersionDirective()) + R"(
        in vec4 vColor;
        in vec2 vUv;

        uniform sampler2D uMask;

        out vec4 FragColor;

        void main()
        {
            float coverage = texture(uMask, vUv).r;
            FragColor = vec4(vColor.rgb, vColor.a * coverage);
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    glGenVertexArrays(1, &VAO_);
    vertexBuffer_.initialize(64);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_.handle());

    constexpr GLsizei stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    initialized_ = true;
}

void DrawClipFillProgram::release(bool abandon)
{
    if (!initialized_) {
        return;
    }

    if (program_ != nullptr) {
        if (abandon) program_->abandonVolatile();
        delete program_;
        program_ = nullptr;
    }

    if (VAO_ != static_cast<unsigned int>(-1)) {
        if (!abandon) glDeleteVertexArrays(1, &VAO_);
        VAO_ = static_cast<unsigned int>(-1);
    }

    if (abandon) vertexBuffer_.abandon(); else vertexBuffer_.release();
    initialized_ = false;
}

void DrawClipFillProgram::draw(const RenderContext &context, const DrawClipFillData &data)
{
    if (!initialized_ || program_ == nullptr) {
        return;
    }
    if (!data.mask || !data.mask->isValid()) {
        return;
    }

    const std::size_t explicitVerts = data.positions ? data.positions->size() / 2u : 0u;
    const bool hasExplicitGeometry = explicitVerts >= 3u && (explicitVerts % 3u) == 0u
                                     && data.uvs != nullptr && data.uvs->size() == data.positions->size();
    const bool hasPerVertexColors = hasExplicitGeometry && data.perVertexColors != nullptr
                                    && data.perVertexColors->size() == explicitVerts * 4u;

    std::vector<float> vertices;
    GLsizei vertexCount = 0;

    if (hasExplicitGeometry) {
        vertexCount = static_cast<GLsizei>(explicitVerts);
        vertices.reserve(explicitVerts * 8u);
        for (std::size_t v = 0; v < explicitVerts; ++v) {
            vertices.push_back((*data.positions)[v * 2u + 0u]);
            vertices.push_back((*data.positions)[v * 2u + 1u]);
            if (hasPerVertexColors) {
                vertices.push_back((*data.perVertexColors)[v * 4u + 0u]);
                vertices.push_back((*data.perVertexColors)[v * 4u + 1u]);
                vertices.push_back((*data.perVertexColors)[v * 4u + 2u]);
                vertices.push_back((*data.perVertexColors)[v * 4u + 3u]);
            } else {
                vertices.push_back(data.color[0]);
                vertices.push_back(data.color[1]);
                vertices.push_back(data.color[2]);
                vertices.push_back(data.color[3]);
            }
            vertices.push_back((*data.uvs)[v * 2u + 0u]);
            vertices.push_back((*data.uvs)[v * 2u + 1u]);
        }
    } else {
        // Full-target quad: 2 triangles, uniform color, uvs cover [0,1].
        const float r = data.color[0], g = data.color[1], b = data.color[2], a = data.color[3];
        vertices = {
            -1.0f, -1.0f, r, g, b, a, 0.0f, 0.0f,
             1.0f, -1.0f, r, g, b, a, 1.0f, 0.0f,
             1.0f,  1.0f, r, g, b, a, 1.0f, 1.0f,
            -1.0f, -1.0f, r, g, b, a, 0.0f, 0.0f,
             1.0f,  1.0f, r, g, b, a, 1.0f, 1.0f,
            -1.0f,  1.0f, r, g, b, a, 0.0f, 1.0f,
        };
        vertexCount = 6;
    }

    program_->use();
    program_->setInt("uMask", 0);

    // Mask textures live off the same ImageResource pipeline; sample linearly
    // so the AA fringe stays smooth, and clamp to avoid wrap sampling into
    // opposite edges when a shape hugs the target boundary.
    context.bindImageResource(data.mask, DrawImageSampling::Linear, DrawImageTileMode::Clamp, false);

    glBindVertexArray(VAO_);
    vertexBuffer_.upload(vertices.data(), vertices.size());
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
}

} // namespace wsc::opengl
