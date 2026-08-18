#pragma once

#include "DrawData.h"
#include "opengl/GLProgram.h"
#include "opengl/StreamBuffer.h"
#include "render/RenderContext.h"

class DrawTextProgram
{
public:
    DrawTextProgram(const DrawTextProgram &) = delete;
    DrawTextProgram &operator=(const DrawTextProgram &) = delete;

    static DrawTextProgram *getInstance()
    {
        if (instance_ == nullptr) {
            instance_ = new DrawTextProgram();
        }
        return instance_;
    }

    ~DrawTextProgram();

    void initialize();
    void release(bool abandon = false);
    void draw(const RenderContext &context, const DrawTextData &data);

private:
    DrawTextProgram();

    static DrawTextProgram *instance_;

    GLProgram *program_ = nullptr;
    unsigned int VAO_ = static_cast<unsigned int>(-1);
    StreamBuffer vertexBuffer_;
    bool initialized_ = false;
};
