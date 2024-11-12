#include "DrawLines.h"
#include <glm.hpp>
#include <gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>
#include <string>

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

    std::string vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;

        out vec4 color;

        void main()
        {
            gl_Position = vec4(aPos, 0.0, 1.0);
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

    glGenVertexArrays(1, &VAO_);
    glGenBuffers(1, &VBO_);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);

    glBufferData(GL_ARRAY_BUFFER, maxLines_ * 6 * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // 预分配vertexCache
    vertexCache_.reserve(maxLines_ * 12);

    initialized_ = true;
}

void DrawLinesProgram::release()
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

void DrawLinesProgram::draw(const RenderContext &context, const DrawLinesData &data)
{
    if (!initialized_)
    {
        std::cerr << "DrawLinesProgram has not been initialized." << std::endl;
        return;
    }

    if (data.points.empty()) {
        return;
    }

    const size_t requiredSize = data.getLineCount() * 6 * 6;
    
    // 只在必要时重新分配缓冲区
    if (requiredSize > maxLines_ * 6 * 6) {
        maxLines_ = requiredSize * BUFFER_GROW_FACTOR;  // 成倍增长策略
        glBindBuffer(GL_ARRAY_BUFFER, VBO_);
        glBufferData(GL_ARRAY_BUFFER, maxLines_ * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        vertexCache_.reserve(maxLines_);
    }

    // 重用vertexCache
    vertexCache_.clear();
    vertexCache_.reserve(requiredSize);

    // 预计算屏幕转换因子
    const float xFactor = 2.0f / context.getWidth();
    const float yFactor = 2.0f / context.getHeight();

    // 批量处理顶点数据
    for (size_t i = 0; i < data.points.size(); i += 4) {
        float x1 = data.points[i];
        float y1 = data.points[i + 1];
        float x2 = data.points[i + 2];
        float y2 = data.points[i + 3];

        // 计算方向向量和法向量
        float dx = x2 - x1;
        float dy = y2 - y1;
        float length = sqrt(dx * dx + dy * dy);
        dx /= length;
        dy /= length;
        float nx = -dy * data.width * 0.5f;
        float ny = dx * data.width * 0.5f;

        // 四个顶点
        float vertices[12] = {
            x1 + nx, y1 + ny,
            x1 - nx, y1 - ny,
            x2 - nx, y2 - ny,
            x2 + nx, y2 + ny,
            x1 + nx, y1 + ny,
            x2 - nx, y2 - ny
        };

        // 转换到 NDC 并添加到 vertexCache_
        for (int j = 0; j < 12; j += 2) {
            float x = vertices[j] * xFactor - 1.0f;
            float y = 1.0f - vertices[j + 1] * yFactor;
            vertexCache_.push_back(x);
            vertexCache_.push_back(y);
            // 添加颜色
            vertexCache_.push_back(data.color[0]);
            vertexCache_.push_back(data.color[1]);
            vertexCache_.push_back(data.color[2]);
            vertexCache_.push_back(data.color[3]);
        }
    }

    program_->use();

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexCache_.size() * sizeof(float), vertexCache_.data());
    glDrawArrays(GL_TRIANGLES, 0, vertexCache_.size() / 6);
    glBindVertexArray(0);
}
