#pragma once

#include "DrawData.h"
#include "opengl/GLProgram.h"
#include "opengl/StreamBuffer.h"
#include "render/RenderContext.h"

class DrawImageProgram
{
public:
    DrawImageProgram(const DrawImageProgram &) = delete;
    DrawImageProgram &operator=(const DrawImageProgram &) = delete;

    static DrawImageProgram *getInstance()
    {
        if (instance_ == nullptr) {
            instance_ = new DrawImageProgram();
        }
        return instance_;
    }

    ~DrawImageProgram();

    void initialize(bool commonProgram);
    void release(bool abandon = false);
    void draw(const RenderContext &context, const DrawImageData &data);

private:
    DrawImageProgram();

    static DrawImageProgram *instance_;

    GLProgram *program_ = nullptr;
    GLProgram *commonProgram_ = nullptr;
    unsigned int VAO_ = static_cast<unsigned int>(-1);
    StreamBuffer vertexBuffer_;
    bool initialized_ = false;
};
