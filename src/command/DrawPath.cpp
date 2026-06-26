
#include "DrawPath.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>
#include "DrawValidation.h"
#include "render/GammaCorrect.h"
#include "opengl/GLShaderSource.h"

DrawPathProgram* DrawPathProgram::instance_ = nullptr;

DrawPathProgram::DrawPathProgram()
{
}

DrawPathProgram::~DrawPathProgram()
{
    release();
}

void DrawPathProgram::initialize()
{
    if (initialized_)
        return;

    // Create the shader program
    std::string vertexSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        uniform mat4 uProjection;
        uniform mat4 uTransform;
        out vec4 vColor;
        void main()
        {
            vColor = aColor;
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
        }
    )";

    std::string fragmentSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        out vec4 FragColor;
        uniform vec4 uColor;
        uniform int uUseVertexColor;
        in vec4 vColor;
        void main()
        {
            FragColor = uUseVertexColor != 0 ? vColor : uColor;
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    // Create the VAO
    glGenVertexArrays(1, &VAO_);

    // Initialize stream buffers
    positionBuffer_.initialize(4096);
    colorBuffer_.initialize(8192);

    // Bind the VAO and configure vertex attributes
    glBindVertexArray(VAO_);

    glBindBuffer(GL_ARRAY_BUFFER, positionBuffer_.handle());
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, colorBuffer_.handle());
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Unbind
    glBindVertexArray(0);

    initialized_ = true;
}

void DrawPathProgram::release()
{
    if (!initialized_)
        return;

    if (program_ != nullptr)
        delete program_;

    if (VAO_ != -1)
        glDeleteVertexArrays(1, &VAO_);

    positionBuffer_.release();
    colorBuffer_.release();

    initialized_ = false;
}

void DrawPathProgram::draw(const RenderContext &context, const DrawPathData &data)
{
    if (!DrawValidation::validateProgram(initialized_, "DrawPathProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateVertexData(data.getPointCount(), "DrawPathProgram::draw")) {
        return;
    }

    // Upload vertex data via stream buffers
    positionBuffer_.upload(data.points.data(), data.points.size());

    if (data.hasVertexColors()) {
        colorBuffer_.upload(data.colors.data(), data.colors.size());
    }

    // Set the projection matrix
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()), static_cast<float>(context.getHeight()), 0.0f);
    program_->use();
    program_->setMat4("uProjection", projection);
    program_->setMat4("uTransform", data.transform);

    // Apply gamma correction to uniform color if enabled.
    float color[4] = {data.color[0], data.color[1], data.color[2], data.color[3]};
    GammaCorrect::srgbToLinear4(color);
    program_->setVec4("uColor", glm::make_vec4(color));
    program_->setInt("uUseVertexColor", data.hasVertexColors() ? 1 : 0);

    // Bind VAO and draw (StreamBuffer already bound the data via upload)
    glBindVertexArray(VAO_);

    if (data.drawMode == PathDrawMode::Fill)
    {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(data.getPointCount()));
    }
    else if (data.drawMode == PathDrawMode::Stroke)
    {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(data.getPointCount()));
    }
    else if (data.drawMode == PathDrawMode::FillAndStroke)
    {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(data.getPointCount()));
    }

    glBindVertexArray(0);
}
