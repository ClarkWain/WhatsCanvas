#pragma once

#include <cstddef>

/// Per-frame rendering statistics for debugging and profiling.
struct FrameStats
{
    std::size_t drawCallCount = 0;       ///< Number of GPU draw calls issued.
    std::size_t commandCount = 0;        ///< Number of commands submitted.
    std::size_t mergedBatchCount = 0;    ///< Number of batch merges performed.
    std::size_t renderTargetSwitches = 0;///< Number of FBO switches.

    void reset()
    {
        drawCallCount = 0;
        commandCount = 0;
        mergedBatchCount = 0;
        renderTargetSwitches = 0;
    }
};
