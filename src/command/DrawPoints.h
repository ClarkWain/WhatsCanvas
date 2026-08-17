#pragma once

#include "DrawData.h"
#include "opengl/GLProgram.h" 
#include "opengl/StreamBuffer.h"

#include "render/RenderContext.h"

class DrawPointsProgram 
{
public:
    // Disable copy construction and copy assignment
    DrawPointsProgram(const DrawPointsProgram&) = delete;
    DrawPointsProgram& operator=(const DrawPointsProgram&) = delete;
    
    // Static accessor for the singleton instance
    static DrawPointsProgram* getInstance() {
        if (instance_ == nullptr) {
            instance_ = new DrawPointsProgram();
        }
        return instance_;
    }

    ~DrawPointsProgram();

    void initialize();
    void release(bool abandon = false);

    void draw(const RenderContext &context, const DrawPointsData &data);

private:
    // Make the constructor private
    DrawPointsProgram();
    
    // Static instance pointer
    static DrawPointsProgram* instance_;
    
    GLProgram* program_ =  nullptr;
    unsigned int VAO_ = -1;
    StreamBuffer vertexBuffer_;

    bool initialized_ = false;

    std::vector<float> vertexCache_;  // Cached vertex data
};
