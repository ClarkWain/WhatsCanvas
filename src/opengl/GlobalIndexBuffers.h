#pragma once

#include <glad/glad.h>

/// Pre-generated static index buffers for quad and fan primitives.
/// These are created once during initialization and reused for all
/// rectangle and circle/arc fill draws, avoiding repeated index
/// generation.
class GlobalIndexBuffers
{
public:
    /// Initialize the global index buffers.
    /// Must be called after OpenGL context creation.
    static void initialize();

    /// Release the global index buffers.
    static void finalize();
    /// Forget names belonging to a lost context without deleting them.
    static void abandon();

    /// Get the quad index buffer handle.
    /// Pattern: [0,1,2, 0,2,3, 4,5,6, 4,6,7, ...]
    static GLuint quadBuffer() { return quadBuffer_; }

    /// Get the fan index buffer handle.
    /// Pattern: [0,1,2, 0,2,3, 0,3,4, ...]
    static GLuint fanBuffer() { return fanBuffer_; }

    /// Get the maximum number of quads supported by the quad buffer.
    static int maxQuads() { return maxQuads_; }

    /// Get the maximum number of fan triangles supported by the fan buffer.
    static int maxFanTriangles() { return maxFanTriangles_; }

private:
    static GLuint quadBuffer_;
    static GLuint fanBuffer_;
    static int maxQuads_;
    static int maxFanTriangles_;
};
