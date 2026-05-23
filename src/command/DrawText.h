#pragma once

#include "DrawData.h"
#include "opengl/GLProgram.h"
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
    void release();
    void draw(const RenderContext &context, const DrawTextData &data);

private:
    DrawTextProgram();

    static DrawTextProgram *instance_;

    GLProgram *program_ = nullptr;
    unsigned int VAO_ = static_cast<unsigned int>(-1);
    unsigned int VBO_ = static_cast<unsigned int>(-1);
    bool initialized_ = false;
};
