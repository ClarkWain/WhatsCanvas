#pragma once

#include <cstddef>

/// Per-frame rendering statistics for debugging and profiling.
struct FrameStats
{
    std::size_t drawCallCount = 0;       ///< Number of GPU draw calls issued.
    std::size_t commandCount = 0;        ///< Number of commands submitted.
    std::size_t mergedBatchCount = 0;    ///< Number of batch merges performed.
    std::size_t renderTargetSwitches = 0;///< Number of FBO switches.
    std::size_t filterCount = 0;         ///< Successful image/backdrop filters.
    std::size_t filterPassCount = 0;     ///< Backend passes used by filters.
    std::size_t downsampledFilterCount = 0; ///< Reduced-resolution filters.
    std::size_t filterInputPixelCount = 0;  ///< Input pixels submitted to filters.
    std::size_t filterPixelPassCount = 0;   ///< Pixels processed across passes.
    std::size_t pathVertexCount = 0;        ///< Path vertices submitted after batching.
    std::size_t pathIndexCount = 0;         ///< Path indices submitted after batching.
    std::size_t pathIndexBytes = 0;         ///< OpenGL path index-stream bytes.
    std::size_t pathUploadCount = 0;        ///< OpenGL path stream uploads.
    std::size_t pathUploadBytes = 0;        ///< OpenGL path stream bytes.
    std::size_t pathTopologyCacheHits = 0;  ///< Reused merged topology packets.
    std::size_t pathTopologyCacheMisses = 0;///< Rebuilt merged topology packets.

    void reset()
    {
        drawCallCount = 0;
        commandCount = 0;
        mergedBatchCount = 0;
        renderTargetSwitches = 0;
        filterCount = 0;
        filterPassCount = 0;
        downsampledFilterCount = 0;
        filterInputPixelCount = 0;
        filterPixelPassCount = 0;
        pathVertexCount = 0;
        pathIndexCount = 0;
        pathIndexBytes = 0;
        pathUploadCount = 0;
        pathUploadBytes = 0;
        pathTopologyCacheHits = 0;
        pathTopologyCacheMisses = 0;
    }
};
