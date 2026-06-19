
#pragma once

#include "DrawData.h"
#include "opengl/GLProgram.h"
#include "render/RenderContext.h"

class DrawPathProgram
{
public:
    // Disable copy construction and copy assignment
    DrawPathProgram(const DrawPathProgram&) = delete;
    DrawPathProgram& operator=(const DrawPathProgram&) = delete;

    // Get the singleton instance
    static DrawPathProgram* getInstance() {
        if (instance_ == nullptr) {
            instance_ = new DrawPathProgram();
        }
        return instance_;
    }

    ~DrawPathProgram();

    void initialize();
    void release();

    void draw(const RenderContext &context, const DrawPathData &data);

private:
    DrawPathProgram();

    static DrawPathProgram* instance_;

    GLProgram* program_ = nullptr;
    unsigned int VAO_ = -1;
    unsigned int VBO_ = -1;
    unsigned int CBO_ = -1;

    bool initialized_ = false;

    int maxVertices_ = 200;
    static constexpr int BUFFER_GROW_FACTOR = 2;

    std::vector<float> vertexCache_;  // Cached vertex data
};
