
#pragma once

#include "DrawData.h"
#include "opengl/GLProgram.h"
#include "opengl/StreamBuffer.h"
#include "opengl/TexelBuffer.h"
#include "render/RenderContext.h"

#include <cstdint>
#include <vector>

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

    void initialize(bool commonProgram);
    void release(bool abandon = false);
    void beginFrame();
    void beginBatch();
    void endBatch();

    void draw(const RenderContext &context, const DrawPathData &data);

    std::size_t frameUploadCount() const { return frameUploadCount_; }
    std::size_t frameUploadBytes() const { return frameUploadBytes_; }
    std::size_t frameIndexBytes() const { return frameIndexBytes_; }
    std::size_t frameUploadedVertexCount() const { return frameUploadedVertexCount_; }
    std::size_t stagingCapacityBytes() const
    {
        return packetScratch_.capacity() * sizeof(std::uint8_t);
    }
private:
    DrawPathProgram();

    static DrawPathProgram* instance_;

    GLProgram* program_ = nullptr;
    GLProgram* commonProgram_ = nullptr;
    GLProgram* activeProgram_ = nullptr;
    unsigned int VAO_ = -1;

    bool initialized_ = false;

    StreamBuffer geometryBuffer_;
    TexelBuffer gradientStopBuffer_;
    TexelBuffer drawParameterBuffer_;
    std::size_t frameUploadCount_ = 0;
    std::size_t frameUploadBytes_ = 0;
    std::size_t frameIndexBytes_ = 0;
    std::size_t frameUploadedVertexCount_ = 0;
    std::vector<std::uint8_t> packetScratch_;
    int projectionWidth_ = -1;
    int projectionHeight_ = -1;
    bool batchActive_ = false;
    bool batchVaoBound_ = false;
    bool hasTransform_ = false;
    glm::mat4 transform_ = glm::mat4(1.0f);
    bool hasUniformColor_ = false;
    glm::vec4 uniformColor_ = glm::vec4(0.0f);
    int useVertexColor_ = -1;
    int useCoverage_ = -1;
    int gradientType_ = -1;
    int useDrawParameters_ = -1;
    int clipEnabled_ = -1;
    int clipMaskUnit_ = -1;
    int clipViewportWidth_ = -1;
    int clipViewportHeight_ = -1;
    bool coverageAttributeEnabled_ = true;
    bool drawIdAttributeEnabled_ = false;
    bool drawParameterTextureBound_ = false;

    void invalidateUniformState();
};
