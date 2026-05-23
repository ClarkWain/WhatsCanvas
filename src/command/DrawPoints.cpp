#include "DrawPoints.h"
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>
#include <string>
#include "opengl/GLProgram.h"

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

    std::string vertexSrc = R"(
        #version 330 core
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

    std::string fragmentSrc = R"(
        #version 330 core
        out vec4 FragColor;

        in vec4 color;

        void main()
        {
            FragColor = color;
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    // 创建VAO和VBO
    glGenVertexArrays(1, &VAO_);
    glGenBuffers(1, &VBO_);

    // 绑定VAO和VBO
    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);

    // 预分配更大的缓冲区大小
    glBufferData(GL_ARRAY_BUFFER, maxPoints_ * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // 设置顶点属性（位置和颜色）
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    // 解绑
    glBindVertexArray(0);

    // 预分配vertexCache
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
    if (!initialized_)
    {
        std::cerr << "DrawPointProgram has not been initialized." << std::endl;
        return;
    }

    if (data.points.empty()) {
        return;
    }

    const size_t requiredSize = data.getPointCount() * 6;
    
    // 只在必要时重新分配缓冲区
    if (requiredSize > maxPoints_ * 6) {
        maxPoints_ = static_cast<int>(requiredSize * BUFFER_GROW_FACTOR);  // 成倍增长策略
        glBindBuffer(GL_ARRAY_BUFFER, VBO_);
        glBufferData(GL_ARRAY_BUFFER, maxPoints_ * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        vertexCache_.reserve(maxPoints_);
    }

    // 重用vertexCache
    vertexCache_.clear();
    vertexCache_.reserve(requiredSize);

    // 批量处理顶点数据
    for (size_t i = 0; i < data.points.size(); i += 2) {
        vertexCache_.push_back(data.points[i]);
        vertexCache_.push_back(data.points[i + 1]);
        vertexCache_.push_back(data.color[0]);
        vertexCache_.push_back(data.color[1]);
        vertexCache_.push_back(data.color[2]);
        vertexCache_.push_back(data.color[3]);
    }

    glEnable(GL_PROGRAM_POINT_SIZE);
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