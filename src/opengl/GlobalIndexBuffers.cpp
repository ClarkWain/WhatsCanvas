#include "GlobalIndexBuffers.h"

#include <vector>

GLuint GlobalIndexBuffers::quadBuffer_ = 0;
GLuint GlobalIndexBuffers::fanBuffer_ = 0;
int GlobalIndexBuffers::maxQuads_ = 0;
int GlobalIndexBuffers::maxFanTriangles_ = 0;

void GlobalIndexBuffers::initialize()
{
    if (quadBuffer_ != 0) {
        return;  // Already initialized.
    }

    // Quad index buffer: each quad = 4 vertices, 6 indices.
    // Pattern: [0,1,2, 0,2,3, 4,5,6, 4,6,7, ...]
    constexpr int kMaxQuads = 4096;
    std::vector<GLuint> quadIndices;
    quadIndices.reserve(static_cast<std::size_t>(kMaxQuads) * 6);
    for (int i = 0; i < kMaxQuads; ++i) {
        const GLuint base = static_cast<GLuint>(i * 4);
        quadIndices.push_back(base + 0);
        quadIndices.push_back(base + 1);
        quadIndices.push_back(base + 2);
        quadIndices.push_back(base + 0);
        quadIndices.push_back(base + 2);
        quadIndices.push_back(base + 3);
    }

    glGenBuffers(1, &quadBuffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadBuffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(quadIndices.size() * sizeof(GLuint)),
                 quadIndices.data(),
                 GL_STATIC_DRAW);
    maxQuads_ = kMaxQuads;

    // Fan index buffer: center vertex is 0, triangles fan out.
    // Pattern: [0,1,2, 0,2,3, 0,3,4, ...]
    constexpr int kMaxFanTriangles = 4096;
    std::vector<GLuint> fanIndices;
    fanIndices.reserve(static_cast<std::size_t>(kMaxFanTriangles) * 3);
    for (int i = 0; i < kMaxFanTriangles; ++i) {
        fanIndices.push_back(0);
        fanIndices.push_back(static_cast<GLuint>(i + 1));
        fanIndices.push_back(static_cast<GLuint>(i + 2));
    }

    glGenBuffers(1, &fanBuffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, fanBuffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(fanIndices.size() * sizeof(GLuint)),
                 fanIndices.data(),
                 GL_STATIC_DRAW);
    maxFanTriangles_ = kMaxFanTriangles;

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void GlobalIndexBuffers::finalize()
{
    if (quadBuffer_ != 0) {
        glDeleteBuffers(1, &quadBuffer_);
        quadBuffer_ = 0;
    }
    if (fanBuffer_ != 0) {
        glDeleteBuffers(1, &fanBuffer_);
        fanBuffer_ = 0;
    }
    maxQuads_ = 0;
    maxFanTriangles_ = 0;
}
