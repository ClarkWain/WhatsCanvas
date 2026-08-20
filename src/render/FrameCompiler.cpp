#include "FrameCompiler.h"

#include <chrono>

namespace {

template <typename T>
std::size_t bytes(const std::vector<T> &values)
{
    return values.size() * sizeof(T);
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
    const auto end = std::chrono::steady_clock::now();
    out.stats = measure(out.packets);
    out.stats.commandCount = commands.size();
    out.stats.cpuTimeNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count());
    return true;
}
