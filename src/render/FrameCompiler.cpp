#include "FrameCompiler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <limits>

namespace {

template <typename T>
std::size_t bytes(const std::vector<T> &values)
{
    return values.size() * sizeof(T);
}

// Keep compiled offscreen batches within the same working-set bound used by
// Renderer::flush().  Beyond this point, reducing one GL submission is not
// worth a large transient packet, especially on mobile stream buffers.
constexpr std::size_t kMaxSolidTriangleBatchVertices = 65535u;

bool sameScissor(const wsc::DrawPrimitive &left,
                 const wsc::DrawPrimitive &right)
{
    return left.scissorEnabled == right.scissorEnabled
        && left.scissorX == right.scissorX
        && left.scissorY == right.scissorY
        && left.scissorWidth == right.scissorWidth
        && left.scissorHeight == right.scissorHeight;
}

bool sameUniformColor(const wsc::DrawPrimitive &left,
                      const wsc::DrawPrimitive &right)
{
    return std::equal(std::begin(left.color), std::end(left.color),
                      std::begin(right.color));
}

bool hasPerVertexColors(const wsc::DrawPrimitive &packet,
                        std::size_t vertexCount)
{
    return packet.colors.size() == vertexCount * 4u;
}

bool hasPerVertexCoverage(const wsc::DrawPrimitive &packet,
                          std::size_t vertexCount)
{
    return packet.coverage.size() == vertexCount;
}

bool canMergeSolidTriangles(const wsc::DrawPrimitive &left,
                            const wsc::DrawPrimitive &right)
{
    if (left.kind != wsc::DrawPrimitiveKind::SolidTriangles
        || right.kind != wsc::DrawPrimitiveKind::SolidTriangles
        || left.blendMode != right.blendMode
        || !sameScissor(left, right)
        || left.clipTexture || right.clipTexture) {
        return false;
    }

    // The shared command encoder currently emits float position/color/coverage
    // streams. Keep the merger representation-safe so future compact encodings
    // are never silently widened or dropped.
    if (!left.compactVertices.empty() || !right.compactVertices.empty()
        || !left.shortIndices.empty() || !right.shortIndices.empty()
        || !left.packedColors.empty() || !right.packedColors.empty()
        || !left.packedCoverage.empty() || !right.packedCoverage.empty()) {
        return false;
    }

    const std::size_t leftVertices = left.positions.size() / 2u;
    const std::size_t rightVertices = right.positions.size() / 2u;
    if (leftVertices == 0 || rightVertices == 0
        || left.positions.size() % 2u != 0u
        || right.positions.size() % 2u != 0u
        || (!left.colors.empty()
            && !hasPerVertexColors(left, leftVertices))
        || (!right.colors.empty()
            && !hasPerVertexColors(right, rightVertices))
        || (!left.coverage.empty()
            && !hasPerVertexCoverage(left, leftVertices))
        || (!right.coverage.empty()
            && !hasPerVertexCoverage(right, rightVertices))
        || leftVertices > kMaxSolidTriangleBatchVertices
        || rightVertices > kMaxSolidTriangleBatchVertices
               - leftVertices) {
        return false;
    }
    const std::size_t maxIndex =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    return leftVertices <= maxIndex
        && rightVertices <= maxIndex - leftVertices;
}

void appendSolidTriangles(wsc::DrawPrimitive &batch,
                          wsc::DrawPrimitive &&incoming)
{
    const std::size_t batchVertices = batch.positions.size() / 2u;
    const std::size_t incomingVertices = incoming.positions.size() / 2u;

    if (!batch.indices.empty() || !incoming.indices.empty()) {
        if (batch.indices.empty()) {
            batch.indices.reserve(batchVertices + incoming.indices.size());
            for (std::size_t vertex = 0; vertex < batchVertices; ++vertex) {
                batch.indices.push_back(static_cast<std::uint32_t>(vertex));
            }
        }
        if (incoming.indices.empty()) {
            // Incoming carried an implicit sequential index range; reserve
            // for that expansion so we do not repeatedly reallocate when
            // batching many mixed-index packets in a row.
            batch.indices.reserve(
                batch.indices.size() + incomingVertices);
            for (std::size_t vertex = 0; vertex < incomingVertices; ++vertex) {
                batch.indices.push_back(static_cast<std::uint32_t>(
                    batchVertices + vertex));
            }
        } else {
            batch.indices.reserve(
                batch.indices.size() + incoming.indices.size());
            for (const std::uint32_t index : incoming.indices) {
                batch.indices.push_back(static_cast<std::uint32_t>(
                    batchVertices + index));
            }
        }
    }

    // Preserve a shared uniform color.  Expanding it to float4 per vertex
    // defeats the compact packet path that the main renderer already uses.
    const bool useVertexColors = hasPerVertexColors(batch, batchVertices)
        || hasPerVertexColors(incoming, incomingVertices)
        || !sameUniformColor(batch, incoming);
    if (useVertexColors && batch.colors.empty()) {
        batch.colors.reserve((batchVertices + incomingVertices) * 4u);
        for (std::size_t vertex = 0; vertex < batchVertices; ++vertex) {
            batch.colors.insert(
                batch.colors.end(), std::begin(batch.color),
                std::end(batch.color));
        }
    }
    if (useVertexColors) {
        if (hasPerVertexColors(incoming, incomingVertices)) {
            batch.colors.insert(batch.colors.end(), incoming.colors.begin(),
                                incoming.colors.end());
        } else {
            for (std::size_t vertex = 0; vertex < incomingVertices; ++vertex) {
                batch.colors.insert(
                    batch.colors.end(), std::begin(incoming.color),
                    std::end(incoming.color));
            }
        }
    }

    if (!batch.coverage.empty() || !incoming.coverage.empty()) {
        if (batch.coverage.empty()) {
            batch.coverage.assign(batchVertices, 1.0f);
        }
        if (incoming.coverage.size() == incomingVertices) {
            batch.coverage.insert(batch.coverage.end(),
                                  incoming.coverage.begin(),
                                  incoming.coverage.end());
        } else {
            batch.coverage.insert(batch.coverage.end(), incomingVertices,
                                  1.0f);
        }
    }

    batch.positions.insert(batch.positions.end(), incoming.positions.begin(),
                           incoming.positions.end());
    batch.compactSolidAttributes = false;
    batch.indicesTrusted = batch.indicesTrusted && incoming.indicesTrusted;
}

void mergeAdjacentSolidTriangles(wsc::DrawList &packets)
{
    wsc::DrawList merged;
    merged.reserve(packets.size());
    for (wsc::DrawPrimitive &packet : packets) {
        if (!merged.empty()
            && canMergeSolidTriangles(merged.back(), packet)) {
            appendSolidTriangles(merged.back(), std::move(packet));
        } else {
            merged.push_back(std::move(packet));
        }
    }
    packets = std::move(merged);
}

} // namespace

FrameCompileStats FrameCompiler::measure(const wsc::DrawList &packets)
{
    FrameCompileStats stats;
    const auto measurePackets = [&stats](const auto &self,
                                         const wsc::DrawList &items) -> void {
        stats.packetCount += items.size();
        for (const wsc::DrawPrimitive &packet : items) {
            stats.vertexBytes += bytes(packet.positions)
                + bytes(packet.compactVertices)
                + bytes(packet.uvs)
                + bytes(packet.texturedInstances)
                + bytes(packet.localPositions)
                + bytes(packet.colors)
                + bytes(packet.packedColors)
                + bytes(packet.packedTints)
                + bytes(packet.coverage)
                + bytes(packet.packedCoverage);
            stats.indexBytes += bytes(packet.indices)
                + bytes(packet.shortIndices);
            stats.textureReferenceCount += packet.texture ? 1u : 0u;
            stats.textureReferenceCount += packet.clipTexture ? 1u : 0u;
            if (!packet.shadowSilhouette.empty()) {
                self(self, packet.shadowSilhouette);
            }
        }
    };
    measurePackets(measurePackets, packets);
    return stats;
}

bool FrameCompiler::compile(
    const std::vector<std::unique_ptr<Command>> &commands,
    const CommandDrawListEncodeRequest &request,
    CompiledFrame &out,
    std::string *error) const
{
    out = {};
    const auto start = std::chrono::steady_clock::now();
    if (!encodeCommandsToDrawList(
            commands, request, out.packets, error)) {
        out.packets.clear();
        return false;
    }
    mergeAdjacentSolidTriangles(out.packets);
    const auto end = std::chrono::steady_clock::now();
    out.stats = measure(out.packets);
    out.stats.commandCount = commands.size();
    out.stats.cpuTimeNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count());
    return true;
}
