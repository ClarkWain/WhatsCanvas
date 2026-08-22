#pragma once

/**
 * @file CanvasStats.h
 * @brief Opt-in detailed rendering diagnostics for Canvas.
 *
 * Include this header only when reading Canvas::getRenderStats(). The common
 * drawing API keeps the large diagnostic field set out of Canvas.h.
 */

#include "Canvas.h"

namespace wsc {

/// Diagnostic snapshot for the most recently recorded/submitted frame.
/// beginFrame() resets per-frame counters. Process/cache lifetime counters
/// are identified explicitly in their field comments.
struct Canvas::RenderStats
{
	std::uint64_t flushCpuTimeNs = 0;
	std::uint64_t frameCompileCpuTimeNs = 0;
	std::uint64_t deviceExecutionCpuTimeNs = 0;
	std::uint64_t gpuTimeNs = 0;
	bool gpuTimeAvailable = false;
	std::size_t commandCount = 0;
	std::size_t drawCallCount = 0;
	std::size_t mergedBatchCount = 0;
	std::size_t compiledPacketCount = 0;
	std::size_t compiledVertexBytes = 0;
	std::size_t compiledIndexBytes = 0;
	std::size_t commandObjectCount = 0;
	std::size_t commandAllocationCount = 0;
	std::size_t commandPoolReuseCount = 0;
	std::size_t commandCloneCount = 0;
	std::size_t payloadCopyBytes = 0;
	std::size_t stagingCapacityBytes = 0;
	std::size_t batchBreakCommandTypeCount = 0;
	std::size_t batchBreakStateCount = 0;
	std::size_t batchBreakTextureLimitCount = 0;
	std::size_t batchBreakVertexLimitCount = 0;
	std::size_t imageBatchQuadCount = 0;
	std::size_t imageBatchInstancedQuadCount = 0;
	std::size_t imageBatchUploadBytes = 0;
	std::size_t renderTargetSwitches = 0;
	std::size_t filterCount = 0;
	std::size_t filterPassCount = 0;
	std::size_t downsampledFilterCount = 0;
	std::size_t filterInputPixelCount = 0;
	std::size_t filterPixelPassCount = 0;
	std::size_t pathVertexCount = 0;
	std::size_t pathInputVertexCount = 0;
	std::size_t pathTessellatedVertexCount = 0;
	std::size_t pathAaExpandedVertexCount = 0;
	std::size_t pathMergedVertexCount = 0;
	std::size_t pathUploadedVertexCount = 0;
	std::size_t pathIndexCount = 0;
	std::size_t pathIndexBytes = 0;
	std::size_t pathUploadCount = 0;
	std::size_t pathUploadBytes = 0;
	std::size_t pathTopologyCacheHits = 0;
	std::size_t pathTopologyCacheMisses = 0;
	std::size_t imageTextureCount = 0;
	std::size_t glyphAtlasTextureCount = 0;
	std::size_t glyphAtlasTextureBytes = 0;
	std::size_t textNormalizationCount = 0;
	std::size_t textShapeCacheHits = 0;
	std::size_t textShapeCacheMisses = 0;
	std::size_t textLayoutCacheHits = 0;
	std::size_t textLayoutCacheMisses = 0;
	std::size_t textLayoutViewHits = 0;
	std::size_t glyphAtlasHits = 0;
	std::size_t glyphAtlasMisses = 0;
	std::size_t glyphRasterizationCount = 0;
	std::size_t zeroAreaGlyphHits = 0;
	std::size_t generatedGlyphQuadCount = 0;
	std::size_t glyphAtlasDirtyBytes = 0;
	/// Per-frame portable text CPU diagnostics. Platform-native adapters may
	/// report zero for stages hidden behind their native text API.
	std::uint64_t textNormalizationCpuTimeNs = 0;
	std::uint64_t textLayoutCacheCpuTimeNs = 0;
	std::uint64_t textShapingCpuTimeNs = 0;
	std::uint64_t glyphCacheLookupCpuTimeNs = 0;
	std::uint64_t glyphRasterCpuTimeNs = 0;
	std::uint64_t glyphAtlasUploadCpuTimeNs = 0;
	std::uint64_t textBidiCpuTimeNs = 0;
	std::uint64_t textFontFallbackCpuTimeNs = 0;
	std::uint64_t textFontDataCpuTimeNs = 0;
	std::uint64_t textShapeEngineCpuTimeNs = 0;
	std::size_t renderTargetCount = 0;
	std::size_t pooledRenderTargetCount = 0;
	std::size_t pooledRenderTargetBytes = 0;
	std::size_t renderTargetPoolReuseCount = 0;
	std::size_t renderTargetPoolAllocationCount = 0;
	std::size_t renderTargetPoolEvictionCount = 0;
	/// Process-lifetime OpenGL program/shader compilation diagnostics.
	/// Other backends report zero until they expose equivalent counters.
	std::size_t shaderProgramLinkCount = 0;
	std::size_t shaderStageCompileCount = 0;
	std::uint64_t shaderCompileCpuTimeNs = 0;
	std::uint64_t shaderLinkCpuTimeNs = 0;
	std::size_t tessellationCacheHits = 0;
	std::size_t tessellationCacheMisses = 0;
	std::size_t tessellationCacheSize = 0;
	std::size_t tessellationCacheBytes = 0;
	std::size_t aaCacheHits = 0;
	std::size_t aaCacheMisses = 0;
	std::size_t aaCacheSize = 0;
	std::size_t aaCacheBytes = 0;
	std::size_t simpleFillPrimitiveCount = 0;
	std::uint64_t simpleFillGeometryCpuTimeNs = 0;
	std::uint64_t simpleFillSubmitCpuTimeNs = 0;
	std::size_t strokeCacheHits = 0;
	std::size_t strokeCacheMisses = 0;
	std::size_t strokeCacheSize = 0;
	std::size_t strokeCacheBytes = 0;
	std::size_t strokeAaCacheHits = 0;
	std::size_t strokeAaCacheMisses = 0;
	std::size_t strokeAaCacheSize = 0;
	std::size_t strokeAaCacheBytes = 0;
	std::size_t bitmapTextCacheSize = 0;
	std::size_t bitmapTextCacheBytes = 0;
	std::size_t retainedPictureCacheHits = 0;
	std::size_t retainedPictureCacheMisses = 0;
	std::size_t retainedPictureRasterCacheHits = 0;
	std::size_t retainedPictureRasterCacheMisses = 0;
	std::size_t retainedPictureRasterCacheSize = 0;
	std::size_t retainedPictureRasterCacheBytes = 0;
	std::size_t retainedPictureRasterCacheEvictions = 0;
	std::uint64_t retainedPictureRasterPrepareCpuTimeNs = 0;
	std::uint64_t retainedPictureRasterBoundsCpuTimeNs = 0;
	std::uint64_t retainedPictureRasterRenderCpuTimeNs = 0;
	std::uint64_t retainedPictureRasterPathCpuTimeNs = 0;
	std::uint64_t retainedPictureRasterTextCpuTimeNs = 0;
	std::uint64_t retainedPictureRasterTextBackendCpuTimeNs = 0;
	std::uint64_t retainedPictureRasterTextAtlasCpuTimeNs = 0;
	/// saveLayer / restoreLayer breakdown. Each field aggregates all layers
	/// touched during the frame. layerBackdropRenderCpuTimeNs counts the
	/// offscreen rerender of pre-layer commands used as backdrop input, so it
	/// scales with the number of backdrop-filter layers times the queued
	/// command volume. layerFilterCpuTimeNs covers the driver-side blocking
	/// portion of blur/inner-shadow filter chains. layerCompositeRenderCpuTimeNs
	/// covers the offscreen render of the layer body itself.
	std::uint64_t layerBackdropRenderCpuTimeNs = 0;
	std::uint64_t layerFilterCpuTimeNs = 0;
	std::uint64_t layerCompositeRenderCpuTimeNs = 0;
	/// Backdrop compile-result cache PoC diagnostics. `backdropFingerprintCpuTimeNs`
	/// measures the cost of hashing the pre-layer command sequence; the counters
	/// track how often that sequence is stable across frames (cache-hit-eligible)
	/// versus divergent (miss) or uncacheable because a command kind currently
	/// has no fingerprint implementation. These fields exist so a follow-up
	/// change can integrate an actual cache without adding more diagnostics.
	std::uint64_t backdropFingerprintCpuTimeNs = 0;
	std::size_t backdropFingerprintStableFrames = 0;
	std::size_t backdropFingerprintDivergentFrames = 0;
	std::size_t backdropFingerprintUncacheable = 0;
	std::size_t trackedResourceBytes = 0;
};

} // namespace wsc
