#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "DrawList.h"
#include "RenderTypes.h"

class Command;

struct CommandDrawListEncodeRequest
{
    int canvasWidth = 0;
    int canvasHeight = 0;
    int targetHeight = 0;
    int scissorOffsetX = 0;
    int scissorOffsetY = 0;

    // Converts a clip-mask state into a sampled coverage texture for backends
    // whose draw-list path implements ClipFill via texture sampling.
    std::function<SharedImageResource(const ClipMaskState &, int, int)> createClipMaskTexture;
};

bool encodeCommandsToDrawList(const std::vector<std::unique_ptr<Command>> &commands,
                              const CommandDrawListEncodeRequest &request,
                              wsc::DrawList &out,
                              std::string *error = nullptr);
