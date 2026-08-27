#include "DrawLines.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>
#include <string>
#include "DrawValidation.h"
#include "opengl/GLShaderSource.h"
#include "opengl/ClipMaskUniforms.h"
#include "render/GammaCorrect.h"

DrawLinesProgram* DrawLinesProgram::instance_ = nullptr;

DrawLinesProgram::DrawLinesProgram()
{
}

DrawLinesProgram::~DrawLinesProgram()
{
    release();
}

void DrawLinesProgram::initialize()
{
    if (initialized_)
    {
        return;
    }

    std::string vertexSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        uniform mat4 uProjection;
        uniform mat4 uTransform;

        out vec4 color;

        void main()
        {
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
            color = aColor;
        }
    )";

    std::string fragmentSrc = std::string(wsc::opengl::shaderVersionDirective())
        + wsc::opengl::clipMaskFragmentUniforms() + R"(
        out vec4 FragColor;

        in vec4 color;

        void main()
        {
            vec4 outColor = color;
            if (uClipEnabled != 0) {
                outColor.a *= texture(uClipMask, gl_FragCoord.xy / uClipViewport).r;
            }
            FragColor = outColor;
        }
    )";

    program_ = new GLProgram("draw_lines", vertexSrc, fragmentSrc);

    glGenVertexArrays(1, &VAO_);
    vertexBuffer_.initialize(7200);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_.handle());

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    vertexCache_.reserve(7200);

    initialized_ = true;
}

void DrawLinesProgram::release(bool abandon)
{
    if (!initialized_)
    {
        return;
    }

    if (program_ != nullptr) {
        if (abandon) program_->abandonVolatile();
        delete program_;
        program_ = nullptr;
    }

    if (!abandon && VAO_ != -1)
        glDeleteVertexArrays(1, &VAO_);
    VAO_ = static_cast<unsigned int>(-1);

    if (abandon) vertexBuffer_.abandon(); else vertexBuffer_.release();

    initialized_ = false;
}

void DrawLinesProgram::draw(const RenderContext &context, const DrawLinesData &data)
{
    initialize();
    if (!DrawValidation::validateProgram(initialized_, "DrawLinesProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateVertexData(data.getLineCount(), "DrawLinesProgram::draw")) {
        return;
    }

    const size_t requiredSize = data.getLineCount() * 6 * 6;

    // Reuse vertexCache
    vertexCache_.clear();
    vertexCache_.reserve(requiredSize);
    float color[4] = {data.color[0], data.color[1], data.color[2], data.color[3]};
    GammaCorrect::srgbToLinear4(color);

    // Process vertex data in batches. Guard on `i + 3` so a `points`
    // buffer whose size is not a multiple of four (from a malformed
    // producer) cannot read past the end of the vector.
    for (size_t i = 0; i + 3 < data.points.size(); i += 4) {
        float x1 = data.points[i];
        float y1 = data.points[i + 1];
        float x2 = data.points[i + 2];
        float y2 = data.points[i + 3];

        // Compute direction and normal vectors
        float dx = x2 - x1;
        float dy = y2 - y1;
        float length = sqrt(dx * dx + dy * dy);
        dx /= length;
        dy /= length;
        float nx = -dy * data.width * 0.5f;
        float ny = dx * data.width * 0.5f;

        // Four quad vertices
        float vertices[12] = {
            x1 + nx, y1 + ny,
            x1 - nx, y1 - ny,
            x2 - nx, y2 - ny,
            x2 + nx, y2 + ny,
            x1 + nx, y1 + ny,
            x2 - nx, y2 - ny
        };

        // Keep pixel-space coordinates and let the shader handle projection/model transforms
        for (int j = 0; j < 12; j += 2) {
            vertexCache_.push_back(vertices[j]);
            vertexCache_.push_back(vertices[j + 1]);
            // Append color values
            vertexCache_.push_back(color[0]);
            vertexCache_.push_back(color[1]);
            vertexCache_.push_back(color[2]);
            vertexCache_.push_back(color[3]);
        }
    }

    program_->use();
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()), static_cast<float>(context.getHeight()), 0.0f);
    program_->setMat4("uProjection", projection);
    program_->setMat4("uTransform", data.transform);
    wsc::opengl::applyClipMaskUniforms(program_, context);

    glBindVertexArray(VAO_);
    vertexBuffer_.upload(vertexCache_.data(), vertexCache_.size());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCache_.size() / 6));
    glBindVertexArray(0);
}
