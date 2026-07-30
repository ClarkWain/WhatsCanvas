#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CommandDrawListEncoder.h"
#include "DrawList.h"

class Command;

struct FrameCompileStats
{
    std::uint64_t cpuTimeNs = 0;
    std::size_t commandCount = 0;
    std::size_t packetCount = 0;
    std::size_t vertexBytes = 0;
    std::size_t indexBytes = 0;
    std::size_t textureReferenceCount = 0;
};

struct CompiledFrame
{
    wsc::DrawList packets;
    FrameCompileStats stats;
};

/// Compiles recorded Canvas commands into the backend-neutral DrawList packet
/// contract. Backends may retain specialized lowering while sharing packet
/// measurement and portable execution through this entry point.
class FrameCompiler
{
public:
    bool compile(
        const std::vector<std::unique_ptr<Command>> &commands,
        const CommandDrawListEncodeRequest &request,
        CompiledFrame &out,
        std::string *error = nullptr) const;

    static FrameCompileStats measure(const wsc::DrawList &packets);
};
