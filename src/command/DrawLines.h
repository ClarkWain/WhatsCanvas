#pragma once

#include "DrawData.h"
#include "opengl/GLProgram.h"
#include "render/RenderContext.h"

class DrawLinesProgram 
{
public:
    DrawLinesProgram(const DrawLinesProgram&) = delete;
    DrawLinesProgram& operator=(const DrawLinesProgram&) = delete;
    
    static DrawLinesProgram* getInstance() {
        if (instance_ == nullptr) {
            instance_ = new DrawLinesProgram();
        }
        return instance_;
    }

    ~DrawLinesProgram();

    void initialize();
    void release();
    void draw(const RenderContext &context, const DrawLinesData &data);

private:
    DrawLinesProgram();
    
    static DrawLinesProgram* instance_;
    
    GLProgram* program_ = nullptr;
    unsigned int VAO_ = -1;
    unsigned int VBO_ = -1;

    bool initialized_ = false;
    int maxLines_ = 1000;
};
