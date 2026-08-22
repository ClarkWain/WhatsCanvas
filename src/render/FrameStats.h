#pragma once

#include <cstddef>
#include <cstdint>

/// Per-frame rendering statistics for debugging and profiling.
struct FrameStats
{
    std::uint64_t flushCpuTimeNs = 0;     ///< Renderer flush wall-clock CPU time.
    std::uint64_t frameCompileCpuTimeNs = 0; ///< Command-to-packet compilation CPU time.
    std::uint64_t deviceExecutionCpuTimeNs = 0; ///< Device command execution CPU time.
    std::uint64_t gpuTimeNs = 0;          ///< Latest completed delayed GPU timer result.
    bool gpuTimeAvailable = false;        ///< Whether gpuTimeNs contains a valid result.
    std::size_t drawCallCount = 0;       ///< Number of GPU draw calls issued.
    std::size_t commandCount = 0;        ///< Number of commands submitted.
    std::size_t mergedBatchCount = 0;    ///< Number of batch merges performed.
    std::size_t compiledPacketCount = 0; ///< Backend-neutral/device packets produced.
    std::size_t compiledVertexBytes = 0; ///< Compiled vertex/attribute payload bytes.
    std::size_t compiledIndexBytes = 0;  ///< Compiled index payload bytes.
    std::size_t commandObjectCount = 0;  ///< Command objects constructed this frame.
    std::size_t commandAllocationCount = 0; ///< Command objects requiring a system heap allocation.
    std::size_t commandPoolReuseCount = 0; ///< Command objects served by a reuse pool.
    std::size_t commandCloneCount = 0;   ///< Command allocations caused by retained/layer cloning.
    std::size_t payloadCopyBytes = 0;    ///< Known CPU payload bytes cloned or materialized.
    std::size_t stagingCapacityBytes = 0;///< Retained CPU command/batch staging capacity.
    std::size_t batchBreakCommandTypeCount = 0; ///< Batch ended at another command kind.
    std::size_t batchBreakStateCount = 0; ///< Batch ended at an incompatible render state.
    std::size_t batchBreakTextureLimitCount = 0; ///< Sprite batch exhausted texture slots.
    std::size_t batchBreakVertexLimitCount = 0; ///< Path batch reached its vertex limit.
    std::size_t imageBatchQuadCount = 0; ///< Image/glyph quads consumed by SpriteBatch.
    std::size_t imageBatchInstancedQuadCount = 0; ///< Quads uploaded as compact instances.
    std::size_t imageBatchUploadBytes = 0; ///< SpriteBatch vertex/instance bytes uploaded.
    std::size_t renderTargetSwitches = 0;///< Number of FBO switches.
    std::size_t filterCount = 0;         ///< Successful image/backdrop filters.
    std::size_t filterPassCount = 0;     ///< Backend passes used by filters.
    std::size_t downsampledFilterCount = 0; ///< Reduced-resolution filters.
    std::size_t filterInputPixelCount = 0;  ///< Input pixels submitted to filters.
    std::size_t filterPixelPassCount = 0;   ///< Pixels processed across passes.
    std::size_t pathVertexCount = 0;        ///< Path vertices submitted after batching.
    std::size_t pathInputVertexCount = 0;   ///< Source contour/polyline vertices consumed.
    std::size_t pathTessellatedVertexCount = 0; ///< Triangle vertices before analytic AA.
    std::size_t pathAaExpandedVertexCount = 0;  ///< Unique vertices after analytic AA.
    std::size_t pathUploadedVertexCount = 0;    ///< Vertex positions uploaded by the backend.
    std::size_t pathIndexCount = 0;         ///< Path indices submitted after batching.
    std::size_t pathIndexBytes = 0;         ///< OpenGL path index-stream bytes.
    std::size_t pathUploadCount = 0;        ///< OpenGL path stream uploads.
    std::size_t pathUploadBytes = 0;        ///< OpenGL path stream bytes.
    std::size_t pathTopologyCacheHits = 0;  ///< Reused merged topology packets.
    std::size_t pathTopologyCacheMisses = 0;///< Rebuilt merged topology packets.
    /// saveLayer breakdown. Each accumulates across every layer restored this frame.
    std::uint64_t layerBackdropRenderCpuTimeNs = 0; ///< Backdrop pre-layer render CPU cost.
    std::uint64_t layerFilterCpuTimeNs = 0;         ///< Filter chain CPU cost per layer.
    std::uint64_t layerCompositeRenderCpuTimeNs = 0;///< Layer-body offscreen render CPU cost.

    void reset()
    {
        flushCpuTimeNs = 0;
        frameCompileCpuTimeNs = 0;
        deviceExecutionCpuTimeNs = 0;
        gpuTimeNs = 0;
        gpuTimeAvailable = false;
        drawCallCount = 0;
        commandCount = 0;
        mergedBatchCount = 0;
        compiledPacketCount = 0;
        compiledVertexBytes = 0;
        compiledIndexBytes = 0;
        commandObjectCount = 0;
        commandAllocationCount = 0;
        commandPoolReuseCount = 0;
        commandCloneCount = 0;
        payloadCopyBytes = 0;
        stagingCapacityBytes = 0;
        batchBreakCommandTypeCount = 0;
        batchBreakStateCount = 0;
        batchBreakTextureLimitCount = 0;
        batchBreakVertexLimitCount = 0;
        imageBatchQuadCount = 0;
        imageBatchInstancedQuadCount = 0;
        imageBatchUploadBytes = 0;
        renderTargetSwitches = 0;
        filterCount = 0;
        filterPassCount = 0;
        downsampledFilterCount = 0;
        filterInputPixelCount = 0;
        filterPixelPassCount = 0;
        pathVertexCount = 0;
        pathInputVertexCount = 0;
        pathTessellatedVertexCount = 0;
        pathAaExpandedVertexCount = 0;
        pathUploadedVertexCount = 0;
        pathIndexCount = 0;
        pathIndexBytes = 0;
        pathUploadCount = 0;
        pathUploadBytes = 0;
        pathTopologyCacheHits = 0;
        pathTopologyCacheMisses = 0;
        layerBackdropRenderCpuTimeNs = 0;
        layerFilterCpuTimeNs = 0;
        layerCompositeRenderCpuTimeNs = 0;
    }
};
