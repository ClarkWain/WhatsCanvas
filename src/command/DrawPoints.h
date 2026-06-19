#pragma once

#include "DrawData.h"
#include "opengl/GLProgram.h" 

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
    void release();

    void draw(const RenderContext &context, const DrawPointsData &data);

private:
    // Make the constructor private
    DrawPointsProgram();
    
    // Static instance pointer
    static DrawPointsProgram* instance_;
    
    GLProgram* program_ =  nullptr;
    unsigned int VAO_ = -1;
    unsigned int VBO_ = -1;

    bool initialized_ = false;

    int maxPoints_ = 200;

    std::vector<float> vertexCache_;  // Cached vertex data
    size_t lastBufferSize_ = 0;       // Previous buffer size
    static constexpr size_t BUFFER_GROW_FACTOR = 2;  // Buffer growth factor
};

