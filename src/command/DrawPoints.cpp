#include "DrawPoints.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>
#include <string>
#include "opengl/GLProgram.h"
#include "opengl/GLShaderSource.h"
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

    std::string fragmentSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        out vec4 FragColor;

        in vec4 color;

        void main()
        {
            FragColor = color;
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    // Create the VAO and VBO
    glGenVertexArrays(1, &VAO_);
    glGenBuffers(1, &VBO_);

    // Bind the VAO and VBO
    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);

    // Preallocate a larger buffer
    glBufferData(GL_ARRAY_BUFFER, maxPoints_ * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // Configure vertex attributes (position and color)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    // Unbind the current objects
    glBindVertexArray(0);

    // Preallocate vertexCache
    vertexCache_.reserve(maxPoints_ * 6);

    initialized_ = true;
}

void DrawPointsProgram::release()
{
    if (!initialized_)
    {
        return;
    }

    if (program_ != nullptr)
        delete program_;

    if (VAO_ != -1)
        glDeleteVertexArrays(1, &VAO_);

    if (VBO_ != -1)
        glDeleteBuffers(1, &VBO_);

    initialized_ = false;
}

void DrawPointsProgram::draw(const RenderContext &context, const DrawPointsData &data)
{
    if (!DrawValidation::validateProgram(initialized_, "DrawPointsProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateVertexData(data.getPointCount(), "DrawPointsProgram::draw")) {
        return;
    }

    const size_t requiredSize = data.getPointCount() * 6;
    
    // Reallocate the buffer only when required
    if (requiredSize > maxPoints_ * 6) {
        maxPoints_ = static_cast<int>(requiredSize * BUFFER_GROW_FACTOR);  // geometric growth policy
        glBindBuffer(GL_ARRAY_BUFFER, VBO_);
        glBufferData(GL_ARRAY_BUFFER, maxPoints_ * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        vertexCache_.reserve(maxPoints_);
    }

    // Reuse vertexCache
    vertexCache_.clear();
    vertexCache_.reserve(requiredSize);

    // Process vertex data in batches
    for (size_t i = 0; i < data.points.size(); i += 2) {
        vertexCache_.push_back(data.points[i]);
        vertexCache_.push_back(data.points[i + 1]);
        vertexCache_.push_back(data.color[0]);
        vertexCache_.push_back(data.color[1]);
        vertexCache_.push_back(data.color[2]);
        vertexCache_.push_back(data.color[3]);
    }

#if !defined(WHATSCANVAS_OPENGL_ES)
    glEnable(GL_PROGRAM_POINT_SIZE);
#endif
    program_->use();
    program_->setFloat("uPointSize", data.size);
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()), static_cast<float>(context.getHeight()), 0.0f);
    program_->setMat4("uProjection", projection);
    program_->setMat4("uTransform", data.transform);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexCache_.size() * sizeof(float), vertexCache_.data());
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(data.getPointCount()));
    glBindVertexArray(0);
}
