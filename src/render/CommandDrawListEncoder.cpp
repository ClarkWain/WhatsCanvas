#include "CommandDrawListEncoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/glm.hpp>

#include "command/DrawCommand.h"
#include "command/DrawData.h"
#include "core/LogInternal.h"

namespace {

void setError(std::string *error, const char *message)
{
    if (error != nullptr) {
        *error = message;
    }
}

int mapBlend(DrawBlendMode mode)
{
    switch (mode) {
    case DrawBlendMode::Src:
        return 1;
    case DrawBlendMode::Add:
        return 2;
    case DrawBlendMode::Multiply:
        return 3;
    case DrawBlendMode::Screen:
        return 4;
    case DrawBlendMode::Dst:
        return 5;
    case DrawBlendMode::Clear:
        return 6;
    case DrawBlendMode::SrcIn:
        return 7;
    case DrawBlendMode::DstIn:
        return 8;
    case DrawBlendMode::SrcOut:
        return 9;
    case DrawBlendMode::DstOut:
        return 10;
    case DrawBlendMode::SrcAtop:
        return 11;
    case DrawBlendMode::DstAtop:
        return 12;
    case DrawBlendMode::Xor:
        return 13;
    default:
        return 0;
    }
}

bool validCanvasSize(const CommandDrawListEncodeRequest &request)
{
    return request.canvasWidth > 0 && request.canvasHeight > 0 && request.targetHeight > 0;
}

void toNdc(const CommandDrawListEncodeRequest &request, const glm::mat4 &tf,
           float x, float y, float &ox, float &oy)
{
    const glm::vec4 p = tf * glm::vec4(x, y, 0.0f, 1.0f);
    ox = p.x / static_cast<float>(request.canvasWidth) * 2.0f - 1.0f;
    oy = p.y / static_cast<float>(request.canvasHeight) * 2.0f - 1.0f;
}

void applyScissor(wsc::DrawPrimitive &prim, const ScissorState &scissor,
                  const CommandDrawListEncodeRequest &request)
{
    if (!scissor.enabled) {
        return;
    }
    const int resolvedX = scissor.x + request.scissorOffsetX;
    const int resolvedY = scissor.y + request.scissorOffsetY;
    prim.scissorEnabled = true;
    prim.scissorX = resolvedX;
    prim.scissorY = request.targetHeight - (resolvedY + scissor.height);
    prim.scissorWidth = scissor.width;
    prim.scissorHeight = scissor.height;
}

void emitQuad(std::vector<float> &out, const CommandDrawListEncodeRequest &request, const glm::mat4 &tf,
              float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy)
{
    float n[8];
    toNdc(request, tf, ax, ay, n[0], n[1]);
    toNdc(request, tf, bx, by, n[2], n[3]);
    toNdc(request, tf, cx, cy, n[4], n[5]);
    toNdc(request, tf, dx, dy, n[6], n[7]);
    const float tris[12] = {n[0], n[1], n[2], n[3], n[4], n[5],
                            n[0], n[1], n[4], n[5], n[6], n[7]};
    out.insert(out.end(), tris, tris + 12);
}

bool pathCoversCanvas(const DrawPathData &d, const CommandDrawListEncodeRequest &request)
{
    if (d.getPointCount() < 3) {
        return false;
    }
    float minX = static_cast<float>(request.canvasWidth);
    float minY = static_cast<float>(request.canvasHeight);
    float maxX = 0.0f;
    float maxY = 0.0f;
    const std::vector<float> &points = d.pointData();
    for (std::size_t i = 0; i < d.getPointCount(); ++i) {
        const glm::vec4 p =
            d.transform
            * glm::vec4(
                points[i * 2 + 0], points[i * 2 + 1],
                0.0f, 1.0f);
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    return minX <= 0.0f && minY <= 0.0f
        && maxX >= static_cast<float>(request.canvasWidth)
        && maxY >= static_cast<float>(request.canvasHeight);
}

void copyGradient(const DrawPathData &src, wsc::DrawPrimitive &dst)
{
    dst.gradientType = static_cast<int>(src.gradientType);
    dst.gradientTileMode = static_cast<int>(src.gradientTileMode);
    dst.linearStart[0] = src.gradientStart[0];
    dst.linearStart[1] = src.gradientStart[1];
    dst.linearEnd[0] = src.gradientEnd[0];
    dst.linearEnd[1] = src.gradientEnd[1];
    dst.radialCenter[0] = src.radialCenter[0];
    dst.radialCenter[1] = src.radialCenter[1];
    dst.radialRadius = src.radialRadius;
    dst.gradientStopCount = src.gradientStopCount;
    const DrawPathGradientStops *stops =
        src.gradientStopData();
    for (int i = 0; i < src.gradientStopCount && i < 8; ++i) {
        dst.gradientStopPositions[i] = stops->positions[i];
        for (int c = 0; c < 4; ++c) {
            dst.gradientStopColors[i * 4 + c] =
                stops->colors[i * 4 + c];
        }
    }
}

void copyGradient(const DrawImageData &src, wsc::DrawPrimitive &dst)
{
    dst.gradientType = static_cast<int>(src.gradientType);
    dst.gradientTileMode = static_cast<int>(src.gradientTileMode);
    dst.linearStart[0] = src.gradientStart[0];
    dst.linearStart[1] = src.gradientStart[1];
    dst.linearEnd[0] = src.gradientEnd[0];
    dst.linearEnd[1] = src.gradientEnd[1];
    dst.radialCenter[0] = src.radialCenter[0];
    dst.radialCenter[1] = src.radialCenter[1];
    dst.radialRadius = src.radialRadius;
    dst.gradientStopCount = src.gradientStopCount;
    for (int i = 0; i < src.gradientStopCount && i < 8; ++i) {
        dst.gradientStopPositions[i] = src.gradientStopPositions[i];
        for (int c = 0; c < 4; ++c) {
            dst.gradientStopColors[i * 4 + c] = src.gradientStopColors[i * 4 + c];
        }
    }
}

void copyGradient(const DrawTextData &src, wsc::DrawPrimitive &dst)
{
    dst.gradientType = static_cast<int>(src.gradientType);
    dst.gradientTileMode = static_cast<int>(src.gradientTileMode);
    dst.linearStart[0] = src.gradientStart[0];
    dst.linearStart[1] = src.gradientStart[1];
    dst.linearEnd[0] = src.gradientEnd[0];
    dst.linearEnd[1] = src.gradientEnd[1];
    dst.radialCenter[0] = src.radialCenter[0];
    dst.radialCenter[1] = src.radialCenter[1];
    dst.radialRadius = src.radialRadius;
    dst.gradientStopCount = src.gradientStopCount;
    for (int i = 0; i < src.gradientStopCount && i < 8; ++i) {
        dst.gradientStopPositions[i] = src.gradientStopPositions[i];
        for (int c = 0; c < 4; ++c) {
            dst.gradientStopColors[i * 4 + c] = src.gradientStopColors[i * 4 + c];
        }
    }
}

bool encodeImage(const DrawImageData &d, const CommandDrawListEncodeRequest &request,
                 wsc::DrawList &out, std::string *error)
{
    if (!d.imageResource || d.clipMask.hasPaths()) {
        setError(error, "image command requires an image resource and currently cannot carry a clip mask");
        return false;
    }
    float nx[4], ny[4];
    toNdc(request, d.transform, d.x, d.y, nx[0], ny[0]);
    toNdc(request, d.transform, d.x + d.width, d.y, nx[1], ny[1]);
    toNdc(request, d.transform, d.x + d.width, d.y + d.height, nx[2], ny[2]);
    toNdc(request, d.transform, d.x, d.y + d.height, nx[3], ny[3]);
    const float uu[4] = {d.u0, d.u1, d.u1, d.u0};
    const float vv[4] = {d.v0, d.v0, d.v1, d.v1};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    wsc::DrawPrimitive prim;
    prim.kind = wsc::DrawPrimitiveKind::TexturedQuad;
    prim.blendMode = mapBlend(d.blendMode);
    applyScissor(prim, d.scissor, request);
    prim.texture = d.imageResource;
    prim.layerAlpha = d.alpha;
    prim.roundedRadius = d.roundedRadius;
    prim.roundedWidth = d.width;
    prim.roundedHeight = d.height;
    prim.tint[0] = d.tintColor[0];
    prim.tint[1] = d.tintColor[1];
    prim.tint[2] = d.tintColor[2];
    prim.tint[3] = d.tintColor[3];
    if (d.hasColorMatrix) {
        prim.hasColorMatrix = true;
        std::memcpy(prim.colorMatrix, d.colorMatrix, sizeof(prim.colorMatrix));
        std::memcpy(prim.colorMatrixOffset, d.colorMatrixOffset, sizeof(prim.colorMatrixOffset));
    }
    if (d.hasShaderGradient()) {
        copyGradient(d, prim);
    }
    prim.sampling = static_cast<int>(d.sampling);
    prim.tileMode = static_cast<int>(d.tileMode);
    prim.useCustomSampler = true;
    prim.positions.reserve(12);
    prim.uvs.reserve(12);
    for (int k : idx) {
        prim.positions.push_back(nx[k]);
        prim.positions.push_back(ny[k]);
        prim.uvs.push_back(uu[k]);
        prim.uvs.push_back(vv[k]);
    }
    out.push_back(std::move(prim));
    return true;
}

bool encodeImageBatch(
    const DrawImageBatchData &d,
    const CommandDrawListEncodeRequest &request,
    wsc::DrawList &out, std::string *error)
{
    if (!d.imageResource || d.clipMask.hasPaths()) {
        setError(
            error,
            "image batch requires an image resource and cannot carry a clip mask");
        return false;
    }
    if (d.quads.empty()) {
        return true;
    }

    wsc::DrawPrimitive prim;
    prim.kind = wsc::DrawPrimitiveKind::TexturedQuad;
    prim.blendMode = mapBlend(d.blendMode);
    applyScissor(prim, d.scissor, request);
    prim.texture = d.imageResource;
    prim.layerAlpha = d.alpha;
    prim.tint[0] = d.tintColor[0];
    prim.tint[1] = d.tintColor[1];
    prim.tint[2] = d.tintColor[2];
    prim.tint[3] = d.tintColor[3];
    prim.sampling = static_cast<int>(DrawImageSampling::Linear);
    prim.tileMode = static_cast<int>(DrawImageTileMode::Clamp);
    prim.useCustomSampler = true;
    prim.positions.reserve(d.quads.size() * 12u);
    prim.uvs.reserve(d.quads.size() * 12u);
    prim.packedTints.reserve(d.quads.size() * 6u);
    constexpr int indices[6] = {0, 1, 2, 0, 2, 3};
    for (const DrawImageBatchQuad &quad : d.quads) {
        float nx[4], ny[4];
        toNdc(request, d.transform, quad.x, quad.y, nx[0], ny[0]);
        toNdc(
            request, d.transform, quad.x + quad.width, quad.y,
            nx[1], ny[1]);
        toNdc(
            request, d.transform, quad.x + quad.width,
            quad.y + quad.height, nx[2], ny[2]);
        toNdc(
            request, d.transform, quad.x, quad.y + quad.height,
            nx[3], ny[3]);
        const float uu[4] = {quad.u0, quad.u1, quad.u1, quad.u0};
        const float vv[4] = {quad.v0, quad.v0, quad.v1, quad.v1};
        for (int index : indices) {
            prim.positions.push_back(nx[index]);
            prim.positions.push_back(ny[index]);
            prim.uvs.push_back(uu[index]);
            prim.uvs.push_back(vv[index]);
            prim.packedTints.push_back(quad.packedTint);
        }
    }
    out.push_back(std::move(prim));
    return true;
}

} // namespace

bool encodeCommandsToDrawList(const std::vector<std::unique_ptr<Command>> &commands,
                              const CommandDrawListEncodeRequest &request,
                              wsc::DrawList &out,
                              std::string *error)
{
    out.clear();
    if (!validCanvasSize(request)) {
        setError(error, "invalid command draw-list encode dimensions");
        return false;
    }

    for (const std::unique_ptr<Command> &cmd : commands) {
        if (!cmd) {
            continue;
        }

        if (cmd->type() == Command::Type::Path) {
            const auto *pathCmd = static_cast<const DrawPathCommand *>(cmd.get());
            const DrawPathData &d = pathCmd->data();
            if (d.clipMask.hasPaths()) {
                if (!request.createClipMaskTexture || d.hasShaderGradient()
                    || d.hasVertexColors() || d.hasCoverage() || !pathCoversCanvas(d, request)) {
                    setError(error, "unsupported clipped path command");
                    return false;
                }
                SharedImageResource maskTexture =
                    request.createClipMaskTexture(d.clipMask, request.canvasWidth, request.canvasHeight);
                if (!maskTexture || !maskTexture->isValid()) {
                    setError(error, "clip mask texture creation failed");
                    return false;
                }
                wsc::DrawPrimitive prim;
                prim.kind = wsc::DrawPrimitiveKind::ClipFill;
                prim.blendMode = mapBlend(d.blendMode);
                applyScissor(prim, d.scissor, request);
                prim.color[0] = d.color[0];
                prim.color[1] = d.color[1];
                prim.color[2] = d.color[2];
                prim.color[3] = d.color[3];
                prim.texture = std::move(maskTexture);
                out.push_back(std::move(prim));
                continue;
            }
            const std::size_t sourceVertexCount = d.getPointCount();
            const std::size_t vertexCount = d.getElementCount();
            if (sourceVertexCount < 3 || vertexCount < 3
                || (vertexCount % 3) != 0) {
                continue;
            }
            const std::vector<float> &points = d.pointData();
            const auto sourceIndex = [&](std::size_t element) {
                return d.hasIndices()
                    ? static_cast<std::size_t>(
                        d.getIndex(element))
                    : element;
            };
            if (d.hasIndices()) {
                for (std::size_t element = 0;
                     element < d.getElementCount(); ++element) {
                    if (d.getIndex(element)
                        >= sourceVertexCount) {
                        setError(
                            error,
                            "path command contains an invalid index");
                        return false;
                    }
                }
            }
            const bool retainIndices =
                d.hasIndices() && !d.hasShaderGradient();
            const std::size_t emittedVertexCount =
                retainIndices ? sourceVertexCount : vertexCount;
            const auto emittedSourceIndex = [&](std::size_t vertex) {
                return retainIndices ? vertex : sourceIndex(vertex);
            };
            wsc::DrawPrimitive prim;
            prim.blendMode = mapBlend(d.blendMode);
            applyScissor(prim, d.scissor, request);
            prim.positions.reserve(emittedVertexCount * 2);
            for (std::size_t i = 0; i < emittedVertexCount; ++i) {
                const std::size_t source = emittedSourceIndex(i);
                float nx = 0.0f, ny = 0.0f;
                toNdc(
                    request, d.transform,
                    points[source * 2 + 0],
                    points[source * 2 + 1], nx, ny);
                prim.positions.push_back(nx);
                prim.positions.push_back(ny);
            }
            if (d.hasShaderGradient()) {
                prim.kind = wsc::DrawPrimitiveKind::GradientFill;
                prim.localPositions.reserve(emittedVertexCount * 2);
                for (std::size_t i = 0;
                     i < emittedVertexCount; ++i) {
                    const std::size_t source =
                        emittedSourceIndex(i);
                    const glm::vec4 p =
                        d.transform
                        * glm::vec4(
                            points[source * 2 + 0],
                            points[source * 2 + 1],
                            0.0f, 1.0f);
                    prim.localPositions.push_back(p.x);
                    prim.localPositions.push_back(p.y);
                }
                copyGradient(d, prim);
            } else {
                prim.kind = wsc::DrawPrimitiveKind::SolidTriangles;
                if (retainIndices) {
                    prim.indices.reserve(vertexCount);
                    for (std::size_t element = 0;
                         element < vertexCount; ++element) {
                        prim.indices.push_back(d.getIndex(element));
                    }
                }
                if (d.hasVertexColors()) {
                    prim.colors.reserve(emittedVertexCount * 4u);
                    for (std::size_t i = 0;
                         i < emittedVertexCount; ++i) {
                        const std::size_t source =
                            emittedSourceIndex(i);
                        for (std::size_t channel = 0; channel < 4; ++channel) {
                            prim.colors.push_back(
                                d.vertexColorAt(source, channel));
                        }
                    }
                }
                if (d.hasCoverage()) {
                    prim.coverage.reserve(emittedVertexCount);
                    for (std::size_t i = 0;
                         i < emittedVertexCount; ++i) {
                        prim.coverage.push_back(
                            d.coverageAt(emittedSourceIndex(i)));
                    }
                }
                prim.color[0] = d.color[0];
                prim.color[1] = d.color[1];
                prim.color[2] = d.color[2];
                prim.color[3] = d.color[3];
            }
            out.push_back(std::move(prim));
        } else if (cmd->type() == Command::Type::Points) {
            const auto *pointsCmd = static_cast<const DrawPointsCommand *>(cmd.get());
            const DrawPointsData &d = pointsCmd->data();
            if (d.clipMask.hasPaths()) {
                setError(error, "unsupported clipped point command");
                return false;
            }
            const std::size_t count = d.getPointCount();
            if (count == 0) {
                continue;
            }
            const float half = (d.size > 0.0f ? d.size : 1.0f) * 0.5f;
            wsc::DrawPrimitive prim;
            prim.kind = wsc::DrawPrimitiveKind::SolidTriangles;
            prim.blendMode = mapBlend(d.blendMode);
            applyScissor(prim, d.scissor, request);
            prim.positions.reserve(count * 12);
            for (std::size_t i = 0; i < count; ++i) {
                const float cx = d.points[i * 2 + 0];
                const float cy = d.points[i * 2 + 1];
                emitQuad(prim.positions, request, d.transform, cx - half, cy - half, cx + half, cy - half,
                         cx + half, cy + half, cx - half, cy + half);
            }
            prim.color[0] = d.color[0];
            prim.color[1] = d.color[1];
            prim.color[2] = d.color[2];
            prim.color[3] = d.color[3];
            out.push_back(std::move(prim));
        } else if (cmd->type() == Command::Type::Lines) {
            const auto *linesCmd = static_cast<const DrawLinesCommand *>(cmd.get());
            const DrawLinesData &d = linesCmd->data();
            if (d.clipMask.hasPaths()) {
                setError(error, "unsupported clipped line command");
                return false;
            }
            const std::size_t lineCount = d.getLineCount();
            if (lineCount == 0) {
                continue;
            }
            const float halfWidth = (d.width > 0.0f ? d.width : 1.0f) * 0.5f;
            wsc::DrawPrimitive prim;
            prim.kind = wsc::DrawPrimitiveKind::SolidTriangles;
            prim.blendMode = mapBlend(d.blendMode);
            applyScissor(prim, d.scissor, request);
            prim.positions.reserve(lineCount * 12);
            for (std::size_t i = 0; i < lineCount; ++i) {
                const float x0 = d.points[i * 4 + 0];
                const float y0 = d.points[i * 4 + 1];
                const float x1 = d.points[i * 4 + 2];
                const float y1 = d.points[i * 4 + 3];
                const float dx = x1 - x0;
                const float dy = y1 - y0;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len < 1e-6f) {
                    continue;
                }
                const float nx = -dy / len * halfWidth;
                const float ny = dx / len * halfWidth;
                emitQuad(prim.positions, request, d.transform, x0 + nx, y0 + ny, x1 + nx, y1 + ny,
                         x1 - nx, y1 - ny, x0 - nx, y0 - ny);
            }
            if (prim.positions.empty()) {
                continue;
            }
            prim.color[0] = d.color[0];
            prim.color[1] = d.color[1];
            prim.color[2] = d.color[2];
            prim.color[3] = d.color[3];
            out.push_back(std::move(prim));
        } else if (cmd->type() == Command::Type::Image) {
            const auto *imageCmd = static_cast<const DrawImageCommand *>(cmd.get());
            if (!encodeImage(imageCmd->data(), request, out, error)) {
                return false;
            }
        } else if (cmd->type() == Command::Type::ImageBatch) {
            const auto *batchCmd =
                static_cast<const DrawImageBatchCommand *>(cmd.get());
            if (!encodeImageBatch(batchCmd->data(), request, out, error)) {
                return false;
            }
        } else if (cmd->type() == Command::Type::Text) {
            const auto *textCmd = static_cast<const DrawTextCommand *>(cmd.get());
            const DrawTextData &d = textCmd->data();
            if (d.clipMask.hasPaths()) {
                setError(error, "unsupported clipped vector text command");
                return false;
            }
            const std::size_t vertexCount = d.getVertexCount();
            if (vertexCount < 3 || (vertexCount % 3) != 0) {
                continue;
            }
            wsc::DrawPrimitive prim;
            prim.blendMode = mapBlend(d.blendMode);
            applyScissor(prim, d.scissor, request);
            prim.positions.reserve(vertexCount * 2);
            for (std::size_t i = 0; i < vertexCount; ++i) {
                float nx = 0.0f, ny = 0.0f;
                toNdc(request, d.transform, d.vertices[i * 2 + 0], d.vertices[i * 2 + 1], nx, ny);
                prim.positions.push_back(nx);
                prim.positions.push_back(ny);
            }
            if (d.hasShaderGradient()) {
                prim.kind = wsc::DrawPrimitiveKind::GradientFill;
                prim.localPositions.assign(d.vertices.begin(),
                                           d.vertices.begin() + static_cast<std::ptrdiff_t>(vertexCount * 2));
                copyGradient(d, prim);
            } else {
                prim.kind = wsc::DrawPrimitiveKind::SolidTriangles;
                prim.color[0] = d.color[0];
                prim.color[1] = d.color[1];
                prim.color[2] = d.color[2];
                prim.color[3] = d.color[3];
            }
            out.push_back(std::move(prim));
        } else {
            setError(error, "unsupported command type");
            return false;
        }
    }

    return true;
}
