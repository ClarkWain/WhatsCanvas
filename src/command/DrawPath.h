
#pragma once

#include "DrawData.h"
#include "opengl/GLProgram.h"
#include "opengl/StreamBuffer.h"
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

    bool initialized_ = false;

    StreamBuffer positionBuffer_;
    StreamBuffer colorBuffer_;
};
