#pragma once

#include "DrawData.h"
#include "opengl/GLProgram.h"
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

    void initialize();
    void release();
    void draw(const RenderContext &context, const DrawImageData &data);

private:
    DrawImageProgram();

    static DrawImageProgram *instance_;

    GLProgram *program_ = nullptr;
    unsigned int VAO_ = static_cast<unsigned int>(-1);
    unsigned int VBO_ = static_cast<unsigned int>(-1);
    bool initialized_ = false;
};
