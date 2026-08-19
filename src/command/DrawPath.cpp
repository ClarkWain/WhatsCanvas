
#include "DrawPath.h"
#include <algorithm>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>
#include "DrawValidation.h"
#include "render/GammaCorrect.h"
#include "opengl/GLShaderSource.h"

DrawPathProgram* DrawPathProgram::instance_ = nullptr;

DrawPathProgram::DrawPathProgram()
{
}

DrawPathProgram::~DrawPathProgram()
{
    release();
}

void DrawPathProgram::initialize(bool commonProgram)
{
    GLProgram *&requestedProgram = commonProgram
        ? commonProgram_ : program_;
    if (requestedProgram != nullptr)
        return;

    // Create the shader program
    std::string vertexSrc = std::string(wsc::opengl::shaderVersionDirective()) +
#if defined(WHATSCANVAS_OPENGL_ES)
    // File-wide highp on GLES: mimic GaussianBlurProgram so shader compilers
    // (including llvmpipe on CI) apply high precision uniformly rather than
    // relying on per-declaration `highp` qualifiers on varyings and uniforms.
    // Required so vLocalPos survives fragment-shader subtraction with
    // uLinearStart when a shape is drawn at large logical y-coordinates.
    R"(
        precision highp float;
        precision highp int;
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        layout (location = 2) in float aCoverage;
        uniform mat4 uProjection;
        uniform mat4 uTransform;
        out vec4 vColor;
        out vec2 vLocalPos;
        out float vCoverage;
        void main()
        {
            vColor = aColor;
            vLocalPos = aPos;
            vCoverage = aCoverage;
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
        }
    )";
#else
    R"(
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        layout (location = 2) in float aCoverage;
        layout (location = 3) in uint aDrawId;
        uniform mat4 uProjection;
        uniform mat4 uTransform;
        uniform int uUseDrawParameters;
        uniform samplerBuffer uDrawParameters;
        out vec4 vColor;
        out vec2 vLocalPos;
        out float vCoverage;

        float drawParameter(int index)
        {
            return texelFetch(uDrawParameters, index).r;
        }

        void main()
        {
            vec2 position = aPos;
            vColor = aColor;
            if (uUseDrawParameters != 0) {
                int base = int(aDrawId) * 10;
                position = vec2(
                    drawParameter(base) * aPos.x
                        + drawParameter(base + 1) * aPos.y
                        + drawParameter(base + 2),
                    drawParameter(base + 3) * aPos.x
                        + drawParameter(base + 4) * aPos.y
                        + drawParameter(base + 5));
                vColor = vec4(
                    drawParameter(base + 6),
                    drawParameter(base + 7),
                    drawParameter(base + 8),
                    drawParameter(base + 9));
            }
            vLocalPos = aPos;
            vCoverage = aCoverage;
            gl_Position =
                uProjection * uTransform
                * vec4(position, 0.0, 1.0);
        }
    )";
#endif

    std::string fragmentSrc = std::string(wsc::opengl::shaderVersionDirective()) +
#if defined(WHATSCANVAS_OPENGL_ES)
        (commonProgram ? "#define WHATSCANVAS_COMMON_PATH 1\n" : "") +
#endif
        wsc::opengl::clipMaskFragmentUniforms() +
#if defined(WHATSCANVAS_OPENGL_ES)
    R"(
        precision highp float;
        precision highp int;
        out vec4 FragColor;
        uniform vec4 uColor;
        uniform int uUseVertexColor;
        uniform int uGradientType;
        #if !defined(WHATSCANVAS_COMMON_PATH)
        uniform int uGradientTileMode;
        #endif
        uniform vec2 uLinearStart;
        uniform vec2 uLinearEnd;
        #if !defined(WHATSCANVAS_COMMON_PATH)
        uniform vec2 uRadialCenter;
        uniform float uRadialRadius;
        #endif
        uniform int uGradientStopCount;
        uniform float uGradientStopPositions[8];
        uniform vec4 uGradientStopColors[8];
        uniform int uUseCoverage;
        in vec4 vColor;
        in vec2 vLocalPos;
        in float vCoverage;

        float applyGradientTile(float t, out float visibility)
        {
            visibility = 1.0;
            #if defined(WHATSCANVAS_COMMON_PATH)
            return clamp(t, 0.0, 1.0);
            #else
            if (uGradientTileMode == 1) {
                return fract(t);
            }
            if (uGradientTileMode == 2) {
                float period = floor(t);
                float localT = t - period;
                if (mod(abs(period), 2.0) > 0.5) {
                    localT = 1.0 - localT;
                }
                return localT;
            }
            if (uGradientTileMode == 3) {
                visibility = (t >= 0.0 && t <= 1.0) ? 1.0 : 0.0;
                return clamp(t, 0.0, 1.0);
            }
            return clamp(t, 0.0, 1.0);
            #endif
        }

        vec4 sampleGradient(float t)
        {
            float visibility = 1.0;
            t = applyGradientTile(t, visibility);
            if (visibility <= 0.0 || uGradientStopCount <= 0) {
                return vec4(0.0);
            }
            if (uGradientStopCount == 1 || t <= uGradientStopPositions[0]) {
                return uGradientStopColors[0] * visibility;
            }
            for (int i = 1; i < 8; ++i) {
                if (i >= uGradientStopCount) {
                    break;
                }
                if (t <= uGradientStopPositions[i]) {
                    float startPos = uGradientStopPositions[i - 1];
                    float endPos = uGradientStopPositions[i];
                    float span = max(endPos - startPos, 0.0001);
                    float localT = clamp((t - startPos) / span, 0.0, 1.0);
                    return mix(uGradientStopColors[i - 1], uGradientStopColors[i], localT) * visibility;
                }
            }
            return uGradientStopColors[uGradientStopCount - 1] * visibility;
        }

        void main()
        {
            vec4 outColor;
            if (uGradientType == 1) {
                vec2 direction = uLinearEnd - uLinearStart;
                float lengthSq = max(dot(direction, direction), 0.0001);
                float t = dot(vLocalPos - uLinearStart, direction) / lengthSq;
                outColor = sampleGradient(t);
            }
            #if !defined(WHATSCANVAS_COMMON_PATH)
            else if (uGradientType == 2) {
                float t = length(vLocalPos - uRadialCenter) / max(uRadialRadius, 0.0001);
                outColor = sampleGradient(t);
            }
            #endif
            else {
                outColor = uUseVertexColor != 0 ? vColor : uColor;
            }
            if (uUseCoverage != 0) {
                outColor.a *= clamp(vCoverage, 0.0, 1.0);
            }
            if (uClipEnabled != 0) {
                outColor.a *= texture(uClipMask, gl_FragCoord.xy / uClipViewport).r;
            }
            FragColor = outColor;
        }
    )";
#else
    R"(
        out vec4 FragColor;
        uniform vec4 uColor;
        uniform int uUseVertexColor;
        uniform int uGradientType;
        uniform int uGradientTileMode;
        uniform vec2 uLinearStart;
        uniform vec2 uLinearEnd;
        uniform vec2 uRadialCenter;
        uniform float uRadialRadius;
        uniform int uGradientStopCount;
        uniform int uUseGradientTexelBuffer;
        uniform samplerBuffer uGradientStops;
        uniform float uGradientStopPositions[8];
        uniform vec4 uGradientStopColors[8];
        uniform int uUseCoverage;
        in vec4 vColor;
        in vec2 vLocalPos;
        in float vCoverage;

        float gradientStopPosition(int index)
        {
            if (uUseGradientTexelBuffer != 0) {
                return texelFetch(uGradientStops, index * 5).r;
            }
            return uGradientStopPositions[index];
        }

        vec4 gradientStopColor(int index)
        {
            if (uUseGradientTexelBuffer != 0) {
                return vec4(
                    texelFetch(uGradientStops, index * 5 + 1).r,
                    texelFetch(uGradientStops, index * 5 + 2).r,
                    texelFetch(uGradientStops, index * 5 + 3).r,
                    texelFetch(uGradientStops, index * 5 + 4).r);
            }
            return uGradientStopColors[index];
        }

        float applyGradientTile(float t, out float visibility)
        {
            visibility = 1.0;
            if (uGradientTileMode == 1) {
                return fract(t);
            }
            if (uGradientTileMode == 2) {
                float period = floor(t);
                float localT = t - period;
                if (mod(abs(period), 2.0) > 0.5) {
                    localT = 1.0 - localT;
                }
                return localT;
            }
            if (uGradientTileMode == 3) {
                visibility = (t >= 0.0 && t <= 1.0) ? 1.0 : 0.0;
                return clamp(t, 0.0, 1.0);
            }
            return clamp(t, 0.0, 1.0);
        }

        vec4 sampleGradient(float t)
        {
            float visibility = 1.0;
            t = applyGradientTile(t, visibility);
            if (visibility <= 0.0 || uGradientStopCount <= 0) {
                return vec4(0.0);
            }
            if (uGradientStopCount == 1 || t <= gradientStopPosition(0)) {
                return gradientStopColor(0) * visibility;
            }
            for (int i = 1; i < 8; ++i) {
                if (i >= uGradientStopCount) {
                    break;
                }
                if (t <= gradientStopPosition(i)) {
                    float startPos = gradientStopPosition(i - 1);
                    float endPos = gradientStopPosition(i);
                    float span = max(endPos - startPos, 0.0001);
                    float localT = clamp((t - startPos) / span, 0.0, 1.0);
                    return mix(gradientStopColor(i - 1), gradientStopColor(i), localT) * visibility;
                }
            }
            return gradientStopColor(uGradientStopCount - 1) * visibility;
        }

        void main()
        {
            vec4 outColor;
            if (uGradientType == 1) {
                vec2 direction = uLinearEnd - uLinearStart;
                float lengthSq = max(dot(direction, direction), 0.0001);
                float t = dot(vLocalPos - uLinearStart, direction) / lengthSq;
                outColor = sampleGradient(t);
            } else if (uGradientType == 2) {
                float t = length(vLocalPos - uRadialCenter) / max(uRadialRadius, 0.0001);
                outColor = sampleGradient(t);
            } else {
                outColor = uUseVertexColor != 0 ? vColor : uColor;
            }
            if (uUseCoverage != 0) {
                outColor.a *= clamp(vCoverage, 0.0, 1.0);
            }
            if (uClipEnabled != 0) {
                outColor.a *= texture(uClipMask, gl_FragCoord.xy / uClipViewport).r;
            }
            FragColor = outColor;
        }
    )";
#endif

    requestedProgram = new GLProgram(
        commonProgram ? "draw_path_common" : "draw_path",
        vertexSrc, fragmentSrc);
    requestedProgram->use();
#if !defined(WHATSCANVAS_OPENGL_ES)
    // Samplers of different types must never alias the same texture unit,
    // even when the branch that samples them is disabled. Mesa validates
    // this at draw time, so reserve stable units up front.
    requestedProgram->setInt("uGradientStops", 1);
    requestedProgram->setInt("uDrawParameters", 2);
#endif

    if (!initialized_) {
        // Create the VAO
        glGenVertexArrays(1, &VAO_);

    // Positions, colors, coverage and indices share one frame stream. This
    // avoids orphaning four separate GL buffers at every frame boundary.
        geometryBuffer_.initialize(16384);

    // Bind the VAO and configure vertex attributes
        glBindVertexArray(VAO_);

        glBindBuffer(GL_ARRAY_BUFFER, geometryBuffer_.handle());
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, geometryBuffer_.handle());
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ARRAY_BUFFER, geometryBuffer_.handle());
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
        glEnableVertexAttribArray(2);
#if !defined(WHATSCANVAS_OPENGL_ES)
        glDisableVertexAttribArray(3);
        glVertexAttribI1ui(3, 0u);
#endif

    // Unbind
        glBindVertexArray(0);

        initialized_ = true;
    }
}

void DrawPathProgram::release(bool abandon)
{
    if (!initialized_)
        return;

    if (program_ != nullptr) {
        if (abandon) program_->abandonVolatile();
        delete program_;
        program_ = nullptr;
    }
    if (commonProgram_ != nullptr) {
        if (abandon) commonProgram_->abandonVolatile();
        delete commonProgram_;
        commonProgram_ = nullptr;
    }
    activeProgram_ = nullptr;

    if (!abandon && VAO_ != -1)
        glDeleteVertexArrays(1, &VAO_);
    VAO_ = static_cast<unsigned int>(-1);

    if (abandon) {
        geometryBuffer_.abandon();
        gradientStopBuffer_.abandon();
        drawParameterBuffer_.abandon();
    } else {
        geometryBuffer_.release();
        gradientStopBuffer_.release();
        drawParameterBuffer_.release();
    }
    projectionWidth_ = -1;
    projectionHeight_ = -1;
    batchActive_ = false;
    batchVaoBound_ = false;
    hasTransform_ = false;
    hasUniformColor_ = false;
    useVertexColor_ = -1;
    useCoverage_ = -1;
    gradientType_ = -1;
    useDrawParameters_ = -1;
    clipEnabled_ = -1;
    clipMaskUnit_ = -1;
    clipViewportWidth_ = -1;
    clipViewportHeight_ = -1;
    coverageAttributeEnabled_ = true;
    drawIdAttributeEnabled_ = false;
    drawParameterTextureBound_ = false;
    initialized_ = false;
}

void DrawPathProgram::invalidateUniformState()
{
    projectionWidth_ = -1;
    projectionHeight_ = -1;
    hasTransform_ = false;
    hasUniformColor_ = false;
    useVertexColor_ = -1;
    useCoverage_ = -1;
    gradientType_ = -1;
    useDrawParameters_ = -1;
    clipEnabled_ = -1;
    clipMaskUnit_ = -1;
    clipViewportWidth_ = -1;
    clipViewportHeight_ = -1;
}

void DrawPathProgram::beginFrame()
{
    frameUploadCount_ = 0;
    frameUploadBytes_ = 0;
    frameIndexBytes_ = 0;
    frameUploadedVertexCount_ = 0;
    geometryBuffer_.beginFrame();
}

void DrawPathProgram::beginBatch()
{
    if (batchActive_) {
        return;
    }
    batchActive_ = true;
    batchVaoBound_ = false;
    // Other command programs may have been used since the previous path
    // batch. Force the first path draw to restore its GL program and cached
    // uniforms even when it selects the same variant as the previous batch.
    activeProgram_ = nullptr;
}

void DrawPathProgram::endBatch()
{
    if (!batchActive_) {
        return;
    }
#if !defined(WHATSCANVAS_OPENGL_ES)
    if (drawParameterTextureBound_) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_BUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
        drawParameterTextureBound_ = false;
    }
#endif
    if (batchVaoBound_) {
        glBindVertexArray(0);
    }
    batchActive_ = false;
    batchVaoBound_ = false;
}

void DrawPathProgram::draw(const RenderContext &context, const DrawPathData &data)
{
    const bool useCommonProgram =
#if defined(WHATSCANVAS_OPENGL_ES)
        data.gradientType != DrawGradientType::Radial
        && data.gradientTileMode == DrawGradientTileMode::Clamp;
#else
        false;
#endif
    initialize(useCommonProgram);
    GLProgram *drawProgram = useCommonProgram
        ? commonProgram_ : program_;
    if (!DrawValidation::validateProgram(
            initialized_ && drawProgram != nullptr,
            "DrawPathProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateVertexData(data.getPointCount(), "DrawPathProgram::draw")) {
        return;
    }

    const std::vector<float> &points = data.pointData();
    const std::vector<float> &coverageData = data.coverageData();
    const std::vector<std::uint8_t> &packedCoverageData =
        data.packedCoverageData();
    const std::vector<std::uint32_t> &indexData = data.indexData();
    if (data.hasIndices()) {
        bool invalidIndex = false;
#if !defined(NDEBUG)
        const std::size_t vertexCount = data.getPointCount();
        for (std::size_t element = 0;
             element < data.getElementCount(); ++element) {
            if (data.getIndex(element) >= vertexCount) {
                invalidIndex = true;
                break;
            }
        }
#endif
        if ((data.getElementCount() % 3u) != 0u || invalidIndex) {
            WSC_LOG_WARN(
                "DrawValidation",
                "DrawPathProgram::draw: invalid triangle index data, draw call skipped.");
            return;
        }
    }
    frameUploadedVertexCount_ += data.getPointCount();
    std::vector<float> linearColors;
    const void *colorSource = nullptr;
    std::size_t colorBytes = 0;
    std::size_t colorAlignment = 1;
    if (data.hasPackedVertexColors()) {
        colorSource = data.packedColors.data();
        colorBytes = data.packedColors.size();
    } else if (data.hasFloatVertexColors()) {
        if (data.vertexColorsLinear) {
            colorSource = data.colors.data();
        } else {
            linearColors = data.colors;
            for (std::size_t i = 0;
                 i + 3 < linearColors.size(); i += 4) {
                GammaCorrect::srgbToLinear4(
                    linearColors.data() + i);
            }
            colorSource = linearColors.data();
        }
        colorBytes = data.colors.size() * sizeof(float);
        colorAlignment = alignof(float);
    }

    const void *coverageSource = nullptr;
    std::size_t coverageBytes = 0;
    std::size_t coverageAlignment = 1;
    if (data.hasPackedCoverage()) {
        coverageSource = packedCoverageData.data();
        coverageBytes = packedCoverageData.size();
    } else if (data.hasFloatCoverage()) {
        coverageSource = coverageData.data();
        coverageBytes = coverageData.size() * sizeof(float);
        coverageAlignment = alignof(float);
    }

    const void *indexSource = nullptr;
    std::size_t indexBytes = 0;
    std::size_t indexAlignment = 1;
    if (data.hasShortIndices()) {
        indexSource = data.shortIndices.data();
        indexBytes =
            data.shortIndices.size() * sizeof(std::uint16_t);
        indexAlignment = alignof(std::uint16_t);
    } else if (data.hasLongIndices()) {
        indexSource = indexData.data();
        indexBytes =
            indexData.size() * sizeof(std::uint32_t);
        indexAlignment = alignof(std::uint32_t);
    }
    const void *drawIdSource = data.hasDrawParameters()
        ? data.drawIds.data() : nullptr;
    const std::size_t drawIdBytes = data.hasDrawParameters()
        ? data.drawIds.size() * sizeof(std::uint16_t) : 0u;

    const auto alignUp = [](std::size_t offset,
                            std::size_t alignment) {
        return (offset + alignment - 1u)
            / alignment * alignment;
    };
    std::size_t packetBytes = 0;
    const auto reserveSection =
        [&](std::size_t bytes, std::size_t alignment) {
            if (bytes == 0) {
                return std::size_t{0};
            }
            packetBytes = alignUp(packetBytes, alignment);
            const std::size_t offset = packetBytes;
            packetBytes += bytes;
            return offset;
        };

    const std::size_t positionBytes =
        points.size() * sizeof(float);
    const std::size_t positionOffset =
        reserveSection(positionBytes, alignof(float));
    const std::size_t colorOffset =
        reserveSection(colorBytes, colorAlignment);
    const std::size_t coverageOffset =
        reserveSection(coverageBytes, coverageAlignment);
    const std::size_t drawIdOffset =
        reserveSection(drawIdBytes, alignof(std::uint16_t));
    const std::size_t indexOffset =
        reserveSection(indexBytes, indexAlignment);

    StreamBuffer::UploadRange positions;
    StreamBuffer::UploadRange colors;
    StreamBuffer::UploadRange coverage;
    StreamBuffer::UploadRange drawIds;
    StreamBuffer::UploadRange indices;
    constexpr std::size_t kCoalescedUploadLimit =
        64u * 1024u;
    if (packetBytes <= kCoalescedUploadLimit) {
        // Keep the initialized prefix at its high-water mark. A frame often
        // alternates between small and large path packets; shrinking here and
        // growing on the next draw makes vector::resize() zero-initialize the
        // same bytes again even though every packet section is overwritten
        // immediately below.
        if (packetScratch_.size() < packetBytes) {
            packetScratch_.resize(packetBytes);
        }
        const auto copySection =
            [&](std::size_t offset, const void *source,
                std::size_t bytes) {
                if (bytes != 0) {
                    std::memcpy(
                        packetScratch_.data() + offset,
                        source, bytes);
                }
            };
        copySection(
            positionOffset, points.data(), positionBytes);
        copySection(colorOffset, colorSource, colorBytes);
        copySection(
            coverageOffset, coverageSource, coverageBytes);
        copySection(drawIdOffset, drawIdSource, drawIdBytes);
        copySection(indexOffset, indexSource, indexBytes);

        geometryBuffer_.reserveAdditionalBytes(packetBytes);
        const StreamBuffer::UploadRange packet =
            geometryBuffer_.uploadBytes(
                packetScratch_.data(), packetBytes);
        ++frameUploadCount_;
        frameUploadBytes_ += packetBytes;
        positions = {
            packet.buffer, packet.byteOffset + positionOffset
        };
        colors = {
            packet.buffer, packet.byteOffset + colorOffset
        };
        coverage = {
            packet.buffer, packet.byteOffset + coverageOffset
        };
        drawIds = {
            packet.buffer, packet.byteOffset + drawIdOffset
        };
        indices = {
            packet.buffer, packet.byteOffset + indexOffset
        };
    } else {
        geometryBuffer_.reserveAdditionalBytes(packetBytes + 16u);
        positions = geometryBuffer_.uploadRange(
            points.data(), points.size());
        ++frameUploadCount_;
        frameUploadBytes_ += positionBytes;
        if (colorBytes != 0) {
            colors = geometryBuffer_.uploadBytes(
                colorSource, colorBytes, colorAlignment);
            ++frameUploadCount_;
            frameUploadBytes_ += colorBytes;
        }
        if (coverageBytes != 0) {
            coverage = geometryBuffer_.uploadBytes(
                coverageSource, coverageBytes,
                coverageAlignment);
            ++frameUploadCount_;
            frameUploadBytes_ += coverageBytes;
        }
        if (drawIdBytes != 0) {
            drawIds = geometryBuffer_.uploadBytes(
                drawIdSource, drawIdBytes,
                alignof(std::uint16_t));
            ++frameUploadCount_;
            frameUploadBytes_ += drawIdBytes;
        }
        if (indexBytes != 0) {
            indices = geometryBuffer_.uploadBytes(
                indexSource, indexBytes, indexAlignment);
            ++frameUploadCount_;
            frameUploadBytes_ += indexBytes;
        }
    }
    frameIndexBytes_ += indexBytes;

    // A retained batch may contain both common and advanced gradients. Each
    // program owns independent uniform state, so force all cached uniforms to
    // be restored after a switch.
    const bool transientBinding = !batchActive_;
    if (transientBinding || activeProgram_ != drawProgram) {
        drawProgram->use();
        activeProgram_ = drawProgram;
        invalidateUniformState();
    }

    // Set the projection matrix
    if (transientBinding) {
        glBindVertexArray(VAO_);
    } else if (!batchVaoBound_) {
        glBindVertexArray(VAO_);
        batchVaoBound_ = true;
    }
    if (projectionWidth_ != context.getWidth()
        || projectionHeight_ != context.getHeight()) {
        const glm::mat4 projection = glm::ortho(
            0.0f, static_cast<float>(context.getWidth()),
            static_cast<float>(context.getHeight()), 0.0f);
        drawProgram->setMat4("uProjection", projection);
        projectionWidth_ = context.getWidth();
        projectionHeight_ = context.getHeight();
    }
    if (!hasTransform_ || transform_ != data.transform) {
        drawProgram->setMat4("uTransform", data.transform);
        transform_ = data.transform;
        hasTransform_ = true;
    }

    bool usingDrawParameters = false;
#if !defined(WHATSCANVAS_OPENGL_ES)
    if (data.hasDrawParameters()) {
        usingDrawParameters = drawParameterBuffer_.update(
            data.drawParameters.data(),
            data.drawParameters.size());
        if (usingDrawParameters) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(
                GL_TEXTURE_BUFFER,
                drawParameterBuffer_.textureHandle());
            glActiveTexture(GL_TEXTURE0);
            drawParameterTextureBound_ = true;
            drawProgram->setInt("uDrawParameters", 2);
            ++frameUploadCount_;
            frameUploadBytes_ +=
                data.drawParameters.size() * sizeof(float);
        }
    }
    const int useDrawParameters =
        usingDrawParameters ? 1 : 0;
    if (useDrawParameters_ != useDrawParameters) {
            drawProgram->setInt(
            "uUseDrawParameters", useDrawParameters);
        useDrawParameters_ = useDrawParameters;
    }
#endif
    const int useVertexColor =
        (data.hasVertexColors() || usingDrawParameters) ? 1 : 0;
    if (useVertexColor_ != useVertexColor) {
        drawProgram->setInt("uUseVertexColor", useVertexColor);
        useVertexColor_ = useVertexColor;
    }
    if (useVertexColor == 0) {
        float color[4] = {
            data.color[0], data.color[1],
            data.color[2], data.color[3]
        };
        GammaCorrect::srgbToLinear4(color);
        const glm::vec4 uniformColor = glm::make_vec4(color);
        if (!hasUniformColor_ || uniformColor_ != uniformColor) {
            drawProgram->setVec4("uColor", uniformColor);
            uniformColor_ = uniformColor;
            hasUniformColor_ = true;
        }
    }
    const int useCoverage = data.hasCoverage() ? 1 : 0;
    if (useCoverage_ != useCoverage) {
        drawProgram->setInt("uUseCoverage", useCoverage);
        useCoverage_ = useCoverage;
    }
    const int clipEnabled =
        context.isClipMaskActive() ? 1 : 0;
    if (clipEnabled_ != clipEnabled) {
        drawProgram->setInt("uClipEnabled", clipEnabled);
        clipEnabled_ = clipEnabled;
    }
    if (clipEnabled != 0) {
        const int clipMaskUnit = context.clipMaskTextureUnit();
        if (clipMaskUnit_ != clipMaskUnit) {
            drawProgram->setInt("uClipMask", clipMaskUnit);
            clipMaskUnit_ = clipMaskUnit;
        }
        if (clipViewportWidth_ != context.getWidth()
            || clipViewportHeight_ != context.getHeight()) {
            drawProgram->setVec2(
                "uClipViewport",
                glm::vec2(
                    static_cast<float>(context.getWidth()),
                    static_cast<float>(context.getHeight())));
            clipViewportWidth_ = context.getWidth();
            clipViewportHeight_ = context.getHeight();
        }
    }
    const int gradientType = static_cast<int>(data.gradientType);
    if (gradientType_ != gradientType) {
        drawProgram->setInt("uGradientType", gradientType);
        gradientType_ = gradientType;
    }
    const bool hasGradient =
        data.gradientType != DrawGradientType::None;
    bool uploadGradientUniforms = hasGradient;
    const DrawPathGradientStops *gradientStopData =
        data.gradientStopData();
    if (hasGradient) {
        if (!useCommonProgram) {
            drawProgram->setInt(
                "uGradientTileMode",
                static_cast<int>(data.gradientTileMode));
        }
        drawProgram->setVec2(
            "uLinearStart",
            glm::vec2(data.gradientStart[0], data.gradientStart[1]));
        drawProgram->setVec2(
            "uLinearEnd",
            glm::vec2(data.gradientEnd[0], data.gradientEnd[1]));
        if (!useCommonProgram) {
            drawProgram->setVec2(
                "uRadialCenter",
                glm::vec2(data.radialCenter[0], data.radialCenter[1]));
            drawProgram->setFloat("uRadialRadius", data.radialRadius);
        }
        drawProgram->setInt(
            "uGradientStopCount", data.gradientStopCount);
    }
#if !defined(WHATSCANVAS_OPENGL_ES)
    bool usingGradientTexelBuffer = false;
    if (data.hasShaderGradient()) {
        std::vector<float> gradientStops;
        gradientStops.reserve(static_cast<std::size_t>(data.gradientStopCount) * 5);
        for (int i = 0; i < data.gradientStopCount; ++i) {
            float stopColor[4] = {
                gradientStopData->colors[i * 4 + 0],
                gradientStopData->colors[i * 4 + 1],
                gradientStopData->colors[i * 4 + 2],
                gradientStopData->colors[i * 4 + 3]
            };
            GammaCorrect::srgbToLinear4(stopColor);
            gradientStops.push_back(
                gradientStopData->positions[i]);
            gradientStops.push_back(stopColor[0]);
            gradientStops.push_back(stopColor[1]);
            gradientStops.push_back(stopColor[2]);
            gradientStops.push_back(stopColor[3]);
        }

        usingGradientTexelBuffer = gradientStopBuffer_.update(gradientStops.data(), gradientStops.size());
        if (usingGradientTexelBuffer) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_BUFFER, gradientStopBuffer_.textureHandle());
            drawProgram->setInt("uGradientStops", 1);
        }
    }
    if (hasGradient) {
        drawProgram->setInt(
            "uUseGradientTexelBuffer",
            usingGradientTexelBuffer ? 1 : 0);
        uploadGradientUniforms = !usingGradientTexelBuffer;
    }
#endif
    const std::size_t gradientUniformCount = std::min(
        static_cast<std::size_t>(
            std::max(0, data.gradientStopCount)),
        DrawPathData::kMaxGradientStops);
    for (std::size_t i = 0;
         uploadGradientUniforms && i < gradientUniformCount; ++i) {
        drawProgram->setFloat(
            "uGradientStopPositions[" + std::to_string(i) + "]",
            gradientStopData->positions[i]);
        float stopColor[4] = {
            gradientStopData->colors[i * 4 + 0],
            gradientStopData->colors[i * 4 + 1],
            gradientStopData->colors[i * 4 + 2],
            gradientStopData->colors[i * 4 + 3]
        };
        GammaCorrect::srgbToLinear4(stopColor);
        drawProgram->setVec4("uGradientStopColors[" + std::to_string(i) + "]", glm::make_vec4(stopColor));
    }

    // Attribute and element-buffer bindings are VAO state. DrawPath can be
    // entered after an offscreen shadow/filter pass, so never rely on the GL
    // bindings that the upload or a previous frame happened to leave behind.
    glBindBuffer(GL_ARRAY_BUFFER, positions.buffer);
    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
        reinterpret_cast<const void *>(positions.byteOffset));
    if (data.hasVertexColors()) {
        if (data.hasPackedVertexColors()) {
            glVertexAttribPointer(
                1, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                4 * sizeof(std::uint8_t),
                reinterpret_cast<const void *>(
                    colors.byteOffset));
        } else {
            glVertexAttribPointer(
                1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                reinterpret_cast<const void *>(colors.byteOffset));
        }
    }

    if (data.hasCoverage()) {
        if (data.hasPackedCoverage()) {
            glVertexAttribPointer(
                2, 1, GL_UNSIGNED_BYTE, GL_TRUE,
                sizeof(std::uint8_t),
                reinterpret_cast<const void *>(
                    coverage.byteOffset));
        } else {
            glVertexAttribPointer(
                2, 1, GL_FLOAT, GL_FALSE, sizeof(float),
                reinterpret_cast<const void *>(coverage.byteOffset));
        }
        if (!coverageAttributeEnabled_) {
            glEnableVertexAttribArray(2);
            coverageAttributeEnabled_ = true;
        }
    } else {
        if (coverageAttributeEnabled_) {
            glDisableVertexAttribArray(2);
            glVertexAttrib1f(2, 1.0f);
            coverageAttributeEnabled_ = false;
        }
    }

#if !defined(WHATSCANVAS_OPENGL_ES)
    if (usingDrawParameters) {
        glVertexAttribIPointer(
            3, 1, GL_UNSIGNED_SHORT,
            sizeof(std::uint16_t),
            reinterpret_cast<const void *>(drawIds.byteOffset));
        if (!drawIdAttributeEnabled_) {
            glEnableVertexAttribArray(3);
            drawIdAttributeEnabled_ = true;
        }
    } else if (drawIdAttributeEnabled_) {
        glDisableVertexAttribArray(3);
        glVertexAttribI1ui(3, 0u);
        drawIdAttributeEnabled_ = false;
    }
#endif

    if (data.hasIndices()) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices.buffer);
        glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(data.getElementCount()),
            data.hasShortIndices()
                ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
            reinterpret_cast<const void *>(indices.byteOffset));
    } else {
        glDrawArrays(
            GL_TRIANGLES, 0,
            static_cast<GLsizei>(data.getPointCount()));
    }

#if !defined(WHATSCANVAS_OPENGL_ES)
    if (data.hasShaderGradient()) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
    }
#endif
    if (transientBinding) {
#if !defined(WHATSCANVAS_OPENGL_ES)
        if (drawParameterTextureBound_) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_BUFFER, 0);
            glActiveTexture(GL_TEXTURE0);
            drawParameterTextureBound_ = false;
        }
#endif
        glBindVertexArray(0);
    }
}
