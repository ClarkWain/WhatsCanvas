#include "DrawPoints.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>
#include <string>
#include "opengl/GLProgram.h"
#include "opengl/GLShaderSource.h"
#include "opengl/ClipMaskUniforms.h"
#include "render/GammaCorrect.h"
#include "DrawValidation.h"

DrawPointsProgram *DrawPointsProgram::instance_ = nullptr;

DrawPointsProgram::DrawPointsProgram()
{
}

DrawPointsProgram::~DrawPointsProgram()
{
    release();
}

void DrawPointsProgram::initialize()
{
    if (initialized_)
    {
        return;
    }

    std::string vertexSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        uniform float uPointSize;
        uniform mat4 uProjection;
        uniform mat4 uTransform;

        out vec4 color;

        void main()
        {
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
            gl_PointSize = uPointSize;
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

    program_ = new GLProgram("draw_points", vertexSrc, fragmentSrc);

    glGenVertexArrays(1, &VAO_);
    vertexBuffer_.initialize(1200);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_.handle());

    // Configure vertex attributes (position and color)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    // Unbind the current objects
    glBindVertexArray(0);

    vertexCache_.reserve(1200);

    initialized_ = true;
}

void DrawPointsProgram::release(bool abandon)
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

void DrawPointsProgram::draw(const RenderContext &context, const DrawPointsData &data)
{
    initialize();
    if (!DrawValidation::validateProgram(initialized_, "DrawPointsProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateVertexData(data.getPointCount(), "DrawPointsProgram::draw")) {
        return;
    }

    const size_t requiredSize = data.getPointCount() * 6;

    // Reuse vertexCache
    vertexCache_.clear();
    vertexCache_.reserve(requiredSize);
    float color[4] = {data.color[0], data.color[1], data.color[2], data.color[3]};
    GammaCorrect::srgbToLinear4(color);

    // Process vertex data in batches. Guard on `i + 1` so an odd-sized
    // `points` buffer (from a malformed producer) cannot read a float past
    // the end of the vector.
    for (size_t i = 0; i + 1 < data.points.size(); i += 2) {
        vertexCache_.push_back(data.points[i]);
        vertexCache_.push_back(data.points[i + 1]);
        vertexCache_.push_back(color[0]);
        vertexCache_.push_back(color[1]);
        vertexCache_.push_back(color[2]);
        vertexCache_.push_back(color[3]);
    }

#if !defined(WHATSCANVAS_OPENGL_ES)
    glEnable(GL_PROGRAM_POINT_SIZE);
#endif
    program_->use();
    program_->setFloat("uPointSize", data.size);
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()), static_cast<float>(context.getHeight()), 0.0f);
    program_->setMat4("uProjection", projection);
    program_->setMat4("uTransform", data.transform);
    wsc::opengl::applyClipMaskUniforms(program_, context);

    glBindVertexArray(VAO_);
    vertexBuffer_.upload(vertexCache_.data(), vertexCache_.size());
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(data.getPointCount()));
    glBindVertexArray(0);
}
