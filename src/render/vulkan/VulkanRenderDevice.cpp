#include "VulkanRenderDevice.h"

#include "../IRenderTarget.h"
#include "../IRenderer.h"
#include "../GaussianKernel.h"
#include "command/DrawCommand.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <iostream>
#include "core/LogInternal.h"
#include <vector>

#if defined(WHATSCANVAS_ENABLE_VULKAN)
#include <array>
#include <cmath>
#include <cstring>
#include <optional>

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <vulkan/vulkan.h>

#include "shaders/SolidShaderSpv.h"
#include "shaders/TexturedShaderSpv.h"
#include "shaders/ClipShaderSpv.h"
#include "shaders/FilterShaderSpv.h"
#include "shaders/GradientShaderSpv.h"
#endif

// ---------------------------------------------------------------------------
// Compiled-in availability
// ---------------------------------------------------------------------------

bool VulkanRenderDevice::isAvailable()
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    // Probe once at runtime: Vulkan is only "available" if the loader can create
    // an instance AND at least one physical device is present. A compile-time-
    // only check would report Vulkan as available on machines that were built
    // with the SDK but have no Vulkan-capable GPU/driver, breaking the public
    // Canvas::isVulkanAvailable() / createVulkan() fallback contract.
    static const bool available = []() -> bool {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;

        VkInstance instance = VK_NULL_HANDLE;
        if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS
            || instance == VK_NULL_HANDLE) {
            return false;
        }
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        vkDestroyInstance(instance, nullptr);
        return deviceCount > 0;
    }();
    return available;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Backend context (M1 render core)
// ---------------------------------------------------------------------------

#if defined(WHATSCANVAS_ENABLE_VULKAN)

// Fragment push constants for the textured pipeline. Layout matches the
// std430-style push_constant block in textured.frag (108 bytes).
struct TexPushConstants
{
    float colorMatrix[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                             0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float colorOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float layerAlpha = 1.0f;
    std::int32_t useColorMatrix = 0;
    std::int32_t sourcePremultiplied = 0;
};

// Uniform buffer for the gradient pipeline (std140-compatible, matches
// GradientUBO in gradient.frag). Size 208 bytes.
struct GradientUBO
{
    float linear[4] = {0.0f, 0.0f, 1.0f, 0.0f};      // start.xy, end.xy
    float radial[4] = {0.0f, 0.0f, 1.0f, 1.0f};      // center.xy, radius, type
    std::int32_t modeCount[4] = {0, 0, 0, 0};        // tileMode, stopCount, _, _
    float stopPos0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float stopPos1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float stopColors[8][4] = {};
};

struct alignas(16) FilterUBO
{
    float directionRadiusDecal[4] = {};
    float colorAdjustment[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    float options[4] = {};
    float packedWeights[68] = {};
};
static_assert(sizeof(FilterUBO) == 320, "FilterUBO must match the shader std140 layout");

struct VulkanRenderDevice::VulkanContext
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::string physicalDeviceName;
    bool deviceReady = false;
    bool presentCapable = false; // surface + swapchain extensions enabled

    // M3/M4 solid-color graphics pipelines (lazily created against a render
    // pass), cached per (topology, blend mode).
    VkShaderModule solidVertModule = VK_NULL_HANDLE;
    VkShaderModule solidFragModule = VK_NULL_HANDLE;
    VkPipelineLayout solidPipelineLayout = VK_NULL_HANDLE;
    struct CachedPipeline
    {
        VkPrimitiveTopology topology;
        int blendMode;
        VkPipeline pipeline;
    };
    std::vector<CachedPipeline> pipelineCache;

    // M5 textured-quad pipeline.
    VkShaderModule texVertModule = VK_NULL_HANDLE;
    VkShaderModule texFragModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout texDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout texPipelineLayout = VK_NULL_HANDLE;
    // Textured pipelines cached per blend mode (the layout + descriptor set
    // layout are shared; only the baked blend state varies).
    struct CachedBlendPipeline
    {
        int blendMode;
        VkPipeline pipeline;
    };
    std::vector<CachedBlendPipeline> texPipelineCache;

    // M7 clip pipeline (position + color + mask UV; samples an R-channel mask).
    VkShaderModule clipVertModule = VK_NULL_HANDLE;
    VkShaderModule clipFragModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout clipDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout clipPipelineLayout = VK_NULL_HANDLE;
    std::vector<CachedBlendPipeline> clipPipelineCache;

    // Gradient pipeline (fragment-evaluated linear/radial gradient via a UBO).
    VkShaderModule gradientVertModule = VK_NULL_HANDLE;
    VkShaderModule gradientFragModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout gradientDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout gradientPipelineLayout = VK_NULL_HANDLE;
    std::vector<CachedBlendPipeline> gradientPipelineCache;

    // Full-screen separable RGBA Gaussian filter pipeline.
    VkShaderModule filterVertModule = VK_NULL_HANDLE;
    VkShaderModule filterFragModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout filterDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout filterPipelineLayout = VK_NULL_HANDLE;
    VkPipeline filterPipeline = VK_NULL_HANDLE;

    // Cached samplers keyed by (filter, address mode) for per-draw sampling/tile
    // modes.
    struct CachedSampler
    {
        VkFilter filter;
        VkSamplerAddressMode addressMode;
        bool mipmap;
        VkSampler sampler;
    };
    std::vector<CachedSampler> samplerCache;

    std::size_t imageTextureCount = 0;

    // Most-recently-activated / filled render target, used as the readback
    // source for readPixelsRGBA(). Mirrors OpenGL's "current framebuffer".
    VkImage readbackImage = VK_NULL_HANDLE;
    VkImageLayout readbackLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    int readbackWidth = 0;
    int readbackHeight = 0;

    std::size_t renderTargetCount = 0;

    // Host-owned external render target (wrap-external). When set, executeCommands
    // renders into it instead of the device-created main target.
    std::unique_ptr<IRenderTarget> externalRenderTarget;

    ~VulkanContext() { destroy(); }

    void destroy()
    {
        // Release the external render target first (it owns view/renderpass/
        // framebuffer on this device, but not the host's image).
        externalRenderTarget.reset();
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            for (CachedPipeline &entry : pipelineCache) {
                if (entry.pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, entry.pipeline, nullptr);
                    entry.pipeline = VK_NULL_HANDLE;
                }
            }
            pipelineCache.clear();
            for (CachedBlendPipeline &entry : texPipelineCache) {
                if (entry.pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, entry.pipeline, nullptr);
                    entry.pipeline = VK_NULL_HANDLE;
                }
            }
            texPipelineCache.clear();
            if (texPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, texPipelineLayout, nullptr);
                texPipelineLayout = VK_NULL_HANDLE;
            }
            if (texDescriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, texDescriptorSetLayout, nullptr);
                texDescriptorSetLayout = VK_NULL_HANDLE;
            }
            if (texVertModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, texVertModule, nullptr);
                texVertModule = VK_NULL_HANDLE;
            }
            if (texFragModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, texFragModule, nullptr);
                texFragModule = VK_NULL_HANDLE;
            }
            for (CachedBlendPipeline &entry : clipPipelineCache) {
                if (entry.pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, entry.pipeline, nullptr);
                    entry.pipeline = VK_NULL_HANDLE;
                }
            }
            clipPipelineCache.clear();
            if (clipPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, clipPipelineLayout, nullptr);
                clipPipelineLayout = VK_NULL_HANDLE;
            }
            if (clipDescriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, clipDescriptorSetLayout, nullptr);
                clipDescriptorSetLayout = VK_NULL_HANDLE;
            }
            if (clipVertModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, clipVertModule, nullptr);
                clipVertModule = VK_NULL_HANDLE;
            }
            if (clipFragModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, clipFragModule, nullptr);
                clipFragModule = VK_NULL_HANDLE;
            }
            for (CachedBlendPipeline &entry : gradientPipelineCache) {
                if (entry.pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, entry.pipeline, nullptr);
                    entry.pipeline = VK_NULL_HANDLE;
                }
            }
            gradientPipelineCache.clear();
            if (gradientPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, gradientPipelineLayout, nullptr);
                gradientPipelineLayout = VK_NULL_HANDLE;
            }
            if (gradientDescriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, gradientDescriptorSetLayout, nullptr);
                gradientDescriptorSetLayout = VK_NULL_HANDLE;
            }
            if (gradientVertModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, gradientVertModule, nullptr);
                gradientVertModule = VK_NULL_HANDLE;
            }
            if (gradientFragModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, gradientFragModule, nullptr);
                gradientFragModule = VK_NULL_HANDLE;
            }
            if (filterPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, filterPipeline, nullptr);
                filterPipeline = VK_NULL_HANDLE;
            }
            if (filterPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, filterPipelineLayout, nullptr);
                filterPipelineLayout = VK_NULL_HANDLE;
            }
            if (filterDescriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, filterDescriptorSetLayout, nullptr);
                filterDescriptorSetLayout = VK_NULL_HANDLE;
            }
            if (filterVertModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, filterVertModule, nullptr);
                filterVertModule = VK_NULL_HANDLE;
            }
            if (filterFragModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, filterFragModule, nullptr);
                filterFragModule = VK_NULL_HANDLE;
            }
            for (CachedSampler &entry : samplerCache) {
                if (entry.sampler != VK_NULL_HANDLE) {
                    vkDestroySampler(device, entry.sampler, nullptr);
                    entry.sampler = VK_NULL_HANDLE;
                }
            }
            samplerCache.clear();
            if (solidPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, solidPipelineLayout, nullptr);
                solidPipelineLayout = VK_NULL_HANDLE;
            }
            if (solidVertModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, solidVertModule, nullptr);
                solidVertModule = VK_NULL_HANDLE;
            }
            if (solidFragModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, solidFragModule, nullptr);
                solidFragModule = VK_NULL_HANDLE;
            }
            if (commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                commandPool = VK_NULL_HANDLE;
            }
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
    }

    VkShaderModule createShaderModule(const std::uint32_t *code, std::size_t byteSize) const
    {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = byteSize;
        info.pCode = code;
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        return module;
    }

    // Fixed-function blend attachment state for a blend-mode index matching
    // VulkanRenderDevice::SolidBlendMode.
    static VkPipelineColorBlendAttachmentState blendStateFor(int blendMode)
    {
        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = VK_TRUE;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                               | VK_COLOR_COMPONENT_A_BIT;
        // Mirror RenderContext::applyBlendMode's glBlendFuncSeparate exactly
        // (GL_FUNC_ADD equation). Indices come from the executeCommands mapBlend
        // lambda; keep 0 == SrcOver as the shared default for pipelines that do
        // not translate a Command blend mode.
        auto set = [&](VkBlendFactor srcC, VkBlendFactor dstC, VkBlendFactor srcA, VkBlendFactor dstA) {
            blend.srcColorBlendFactor = srcC;
            blend.dstColorBlendFactor = dstC;
            blend.srcAlphaBlendFactor = srcA;
            blend.dstAlphaBlendFactor = dstA;
        };
        switch (blendMode) {
        case 1: // Src: (ONE, ZERO, ONE, ZERO)
            set(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
            break;
        case 2: // Add: (SRC_ALPHA, ONE, ONE, ONE)
            set(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE);
            break;
        case 3: // Multiply: (DST_COLOR, ZERO, DST_ALPHA, ZERO)
            set(VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_DST_ALPHA,
                VK_BLEND_FACTOR_ZERO);
            break;
        case 4: // Screen: (ONE, ONE_MINUS_SRC_COLOR, ONE, ONE_MINUS_SRC_ALPHA)
            set(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_ONE,
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
            break;
        case 5: // Dst: (ZERO, ONE, ZERO, ONE)
            set(VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE);
            break;
        case 6: // Clear: (ZERO, ZERO, ZERO, ZERO)
            set(VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO);
            break;
        case 7: // SrcIn: (DST_ALPHA, ZERO, DST_ALPHA, ZERO)
            set(VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_DST_ALPHA,
                VK_BLEND_FACTOR_ZERO);
            break;
        case 8: // DstIn: (ZERO, SRC_ALPHA, ZERO, SRC_ALPHA)
            set(VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ZERO,
                VK_BLEND_FACTOR_SRC_ALPHA);
            break;
        case 9: // SrcOut: (ONE_MINUS_DST_ALPHA, ZERO, ONE_MINUS_DST_ALPHA, ZERO)
            set(VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ZERO,
                VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ZERO);
            break;
        case 10: // DstOut: (ZERO, ONE_MINUS_SRC_ALPHA, ZERO, ONE_MINUS_SRC_ALPHA)
            set(VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ZERO,
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
            break;
        case 11: // SrcAtop: (DST_ALPHA, ONE_MINUS_SRC_ALPHA, DST_ALPHA, ONE_MINUS_SRC_ALPHA)
            set(VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA,
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
            break;
        case 12: // DstAtop: (ONE_MINUS_DST_ALPHA, SRC_ALPHA, ONE_MINUS_DST_ALPHA, SRC_ALPHA)
            set(VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA,
                VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA);
            break;
        case 13: // Xor: (ONE_MINUS_DST_ALPHA, ONE_MINUS_SRC_ALPHA, ...)
            set(VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
            break;
        case 0: // SrcOver: (SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE_MINUS_SRC_ALPHA)
        default:
            set(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE,
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
            break;
        }
        return blend;
    }

    // Lazily build (and cache) a solid-color pipeline for the given topology and
    // blend mode. All render targets use compatible render passes, so pipelines
    // are reused across targets. Returns VK_NULL_HANDLE on failure.
    VkPipeline ensureSolidPipeline(VkRenderPass renderPass, VkPrimitiveTopology topology, int blendMode)
    {
        for (const CachedPipeline &entry : pipelineCache) {
            if (entry.topology == topology && entry.blendMode == blendMode) {
                return entry.pipeline;
            }
        }

        if (solidVertModule == VK_NULL_HANDLE) {
            solidVertModule = createShaderModule(kSolidVertSpv, sizeof(kSolidVertSpv));
        }
        if (solidFragModule == VK_NULL_HANDLE) {
            solidFragModule = createShaderModule(kSolidFragSpv, sizeof(kSolidFragSpv));
        }
        if (solidVertModule == VK_NULL_HANDLE || solidFragModule == VK_NULL_HANDLE) {
            return VK_NULL_HANDLE;
        }

        if (solidPipelineLayout == VK_NULL_HANDLE) {
            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &solidPipelineLayout) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = solidVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = solidFragModule;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = 7 * sizeof(float);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attrs{};
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = 0;
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[1].offset = 2 * sizeof(float);
        attrs[2].location = 2;
        attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32_SFLOAT;
        attrs[2].offset = 6 * sizeof(float);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
        vertexInput.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = topology;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment = blendStateFor(blendMode);

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;

        const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = solidPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        pipelineCache.push_back({topology, blendMode, pipeline});
        return pipeline;
    }

    std::optional<std::uint32_t> findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        for (std::uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            const bool typeAllowed = (typeBits & (1u << i)) != 0;
            const bool hasProps = (memProps.memoryTypes[i].propertyFlags & properties) == properties;
            if (typeAllowed && hasProps) {
                return i;
            }
        }
        return std::nullopt;
    }

    VkCommandBuffer beginSingleTimeCommands() const
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device, &allocInfo, &cmd) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);
        return cmd;
    }

    bool endSingleTimeCommands(VkCommandBuffer cmd) const
    {
        if (cmd == VK_NULL_HANDLE) {
            return false;
        }
        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(device, &fenceInfo, nullptr, &fence);

        const VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence);
        if (submitResult == VK_SUCCESS) {
            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        }

        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
        return submitResult == VK_SUCCESS;
    }

    // Broad, synchronous layout transition. Simple and correct for the
    // single-time submits used in M1/M2; tighter barriers come with the
    // per-frame pipeline in later milestones.
    static void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                                      VkImageLayout newLayout)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
    }

    bool createHostVisibleBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer &outBuffer,
                                 VkDeviceMemory &outMemory) const
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(device, outBuffer, &memReq);
        const auto typeIndex = findMemoryType(memReq.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                                  | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!typeIndex.has_value()) {
            vkDestroyBuffer(device, outBuffer, nullptr);
            outBuffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = typeIndex.value();
        if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
            vkDestroyBuffer(device, outBuffer, nullptr);
            outBuffer = VK_NULL_HANDLE;
            return false;
        }

        vkBindBufferMemory(device, outBuffer, outMemory, 0);
        return true;
    }

    void copyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, int x, int y, int width,
                           int height) const
    {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {x, y, 0};
        region.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    // Return a cached sampler for the given filter and address mode.
    VkSampler getSampler(VkFilter filter, VkSamplerAddressMode addressMode, bool mipmap = false)
    {
        for (const CachedSampler &entry : samplerCache) {
            if (entry.filter == filter && entry.addressMode == addressMode && entry.mipmap == mipmap) {
                return entry.sampler;
            }
        }
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = filter;
        info.minFilter = filter;
        info.mipmapMode = mipmap ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = addressMode;
        info.addressModeV = addressMode;
        info.addressModeW = addressMode;
        info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK; // decal
        info.maxLod = mipmap ? VK_LOD_CLAMP_NONE : 0.0f;
        VkSampler sampler = VK_NULL_HANDLE;
        if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        samplerCache.push_back({filter, addressMode, mipmap, sampler});
        return sampler;
    }

    // Lazily build the textured-quad pipeline (combined image sampler at
    // binding 0), reused across compatible render passes.
    // Lazily build (and cache) a textured pipeline for the given blend mode. The
    // pipeline layout + descriptor set layout are created once and shared.
    // Returns VK_NULL_HANDLE on failure.
    VkPipeline ensureTexturePipeline(VkRenderPass renderPass, int blendMode)
    {
        for (const CachedBlendPipeline &entry : texPipelineCache) {
            if (entry.blendMode == blendMode) {
                return entry.pipeline;
            }
        }

        if (texPipelineLayout == VK_NULL_HANDLE) {
            texVertModule = createShaderModule(kTexturedVertSpv, sizeof(kTexturedVertSpv));
            texFragModule = createShaderModule(kTexturedFragSpv, sizeof(kTexturedFragSpv));
            if (texVertModule == VK_NULL_HANDLE || texFragModule == VK_NULL_HANDLE) {
                return VK_NULL_HANDLE;
            }

            VkDescriptorSetLayoutBinding samplerBinding{};
            samplerBinding.binding = 0;
            samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerBinding.descriptorCount = 1;
            samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo dslInfo{};
            dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            dslInfo.bindingCount = 1;
            dslInfo.pBindings = &samplerBinding;
            if (vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &texDescriptorSetLayout) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &texDescriptorSetLayout;
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(TexPushConstants);
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &texPipelineLayout) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = texVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = texFragModule;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = 4 * sizeof(float);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attrs{};
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = 0;
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset = 2 * sizeof(float);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
        vertexInput.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment = blendStateFor(blendMode);

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;

        const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = texPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline)
            != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        texPipelineCache.push_back({blendMode, pipeline});
        return pipeline;
    }

    VkPipeline ensureFilterPipeline(VkRenderPass renderPass)
    {
        if (filterPipeline != VK_NULL_HANDLE) {
            return filterPipeline;
        }

        auto resetPartialState = [&]() {
            if (filterPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, filterPipeline, nullptr);
                filterPipeline = VK_NULL_HANDLE;
            }
            if (filterPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, filterPipelineLayout, nullptr);
                filterPipelineLayout = VK_NULL_HANDLE;
            }
            if (filterDescriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, filterDescriptorSetLayout, nullptr);
                filterDescriptorSetLayout = VK_NULL_HANDLE;
            }
            if (filterVertModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, filterVertModule, nullptr);
                filterVertModule = VK_NULL_HANDLE;
            }
            if (filterFragModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, filterFragModule, nullptr);
                filterFragModule = VK_NULL_HANDLE;
            }
        };
        resetPartialState();

        filterVertModule = createShaderModule(kFilterVertSpv, sizeof(kFilterVertSpv));
        filterFragModule = createShaderModule(kFilterFragSpv, sizeof(kFilterFragSpv));
        if (filterVertModule == VK_NULL_HANDLE || filterFragModule == VK_NULL_HANDLE) {
            resetPartialState();
            return VK_NULL_HANDLE;
        }

        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{};
        descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorLayoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        descriptorLayoutInfo.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr,
                                        &filterDescriptorSetLayout) != VK_SUCCESS) {
            resetPartialState();
            return VK_NULL_HANDLE;
        }

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &filterDescriptorSetLayout;
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &filterPipelineLayout) != VK_SUCCESS) {
            resetPartialState();
            return VK_NULL_HANDLE;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = filterVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = filterFragModule;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment = blendStateFor(1);
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;

        const std::array<VkDynamicState, 2> dynamicStates{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = filterPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                      &filterPipeline) != VK_SUCCESS) {
            resetPartialState();
        }
        return filterPipeline;
    }

    // Lazily build (and cache) a clip pipeline for the given blend mode
    // (position + color + mask UV; combined image sampler at binding 0).
    VkPipeline ensureClipPipeline(VkRenderPass renderPass, int blendMode)
    {
        for (const CachedBlendPipeline &entry : clipPipelineCache) {
            if (entry.blendMode == blendMode) {
                return entry.pipeline;
            }
        }

        if (clipPipelineLayout == VK_NULL_HANDLE) {
            clipVertModule = createShaderModule(kClipVertSpv, sizeof(kClipVertSpv));
            clipFragModule = createShaderModule(kClipFragSpv, sizeof(kClipFragSpv));
            if (clipVertModule == VK_NULL_HANDLE || clipFragModule == VK_NULL_HANDLE) {
                return VK_NULL_HANDLE;
            }

            VkDescriptorSetLayoutBinding samplerBinding{};
            samplerBinding.binding = 0;
            samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerBinding.descriptorCount = 1;
            samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutCreateInfo dslInfo{};
            dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            dslInfo.bindingCount = 1;
            dslInfo.pBindings = &samplerBinding;
            if (vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &clipDescriptorSetLayout) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &clipDescriptorSetLayout;
            if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &clipPipelineLayout) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = clipVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = clipFragModule;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = 8 * sizeof(float);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attrs{};
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = 0;
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[1].offset = 2 * sizeof(float);
        attrs[2].location = 2;
        attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = 6 * sizeof(float);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
        vertexInput.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment = blendStateFor(blendMode);

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;

        const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = clipPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline)
            != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        clipPipelineCache.push_back({blendMode, pipeline});
        return pipeline;
    }

    // Lazily build (and cache) a gradient pipeline for the given blend mode
    // (position + canvas-space local; UBO with gradient parameters + stops).
    VkPipeline ensureGradientPipeline(VkRenderPass renderPass, int blendMode)
    {
        for (const CachedBlendPipeline &entry : gradientPipelineCache) {
            if (entry.blendMode == blendMode) {
                return entry.pipeline;
            }
        }

        if (gradientPipelineLayout == VK_NULL_HANDLE) {
            gradientVertModule = createShaderModule(kGradientVertSpv, sizeof(kGradientVertSpv));
            gradientFragModule = createShaderModule(kGradientFragSpv, sizeof(kGradientFragSpv));
            if (gradientVertModule == VK_NULL_HANDLE || gradientFragModule == VK_NULL_HANDLE) {
                return VK_NULL_HANDLE;
            }

            VkDescriptorSetLayoutBinding uboBinding{};
            uboBinding.binding = 0;
            uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboBinding.descriptorCount = 1;
            uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutCreateInfo dslInfo{};
            dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            dslInfo.bindingCount = 1;
            dslInfo.pBindings = &uboBinding;
            if (vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &gradientDescriptorSetLayout)
                != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &gradientDescriptorSetLayout;
            if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &gradientPipelineLayout) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = gradientVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = gradientFragModule;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = 4 * sizeof(float);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attrs{};
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = 0;
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset = 2 * sizeof(float);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
        vertexInput.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment = blendStateFor(blendMode);

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;

        const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = gradientPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline)
            != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        gradientPipelineCache.push_back({blendMode, pipeline});
        return pipeline;
    }
};

namespace {

constexpr VkFormat kRenderTargetFormat = VK_FORMAT_R8G8B8A8_UNORM;

// Minimal non-owning image resource; the owning render target manages the image
// lifetime. Sampling support arrives with the texture milestone (M5).
class VulkanImageResource final : public ImageResource
{
public:
    VulkanImageResource(VkImage image, int width, int height)
        : image_(image), width_(width), height_(height)
    {
    }

    bool isValid() const override { return image_ != VK_NULL_HANDLE; }
    ImageAlphaType alphaType() const override
    {
        return ImageAlphaType::Premultiplied;
    }
    void bind(const RenderContext & /*context*/) const override {}
    bool updateRGBA(int /*x*/, int /*y*/, int /*width*/, int /*height*/, const unsigned char * /*pixels*/,
                    bool /*regenerateMipmaps*/) override
    {
        return false;
    }

    VkImage image() const { return image_; }

private:
    VkImage image_ = VK_NULL_HANDLE;
    int width_ = 0;
    int height_ = 0;
};

// Owning sampled texture: image + memory + view + sampler.
class VulkanTextureResource final : public ImageResource
{
public:
    VulkanTextureResource(VulkanRenderDevice::VulkanContext *context, VkImage image, VkDeviceMemory memory,
                          VkImageView view, VkSampler sampler, int width, int height, int mipLevels = 1,
                          bool ownsImage = true,
                          ImageAlphaType alphaType = ImageAlphaType::Straight)
        : context_(context),
          image_(image),
          memory_(memory),
          view_(view),
          sampler_(sampler),
          width_(width),
          height_(height),
          mipLevels_(mipLevels),
          ownsImage_(ownsImage),
          alphaType_(alphaType)
    {
        if (context_) {
            ++context_->imageTextureCount;
        }
    }

    ~VulkanTextureResource() override
    {
        if (!context_ || context_->device == VK_NULL_HANDLE) {
            return;
        }
        vkDeviceWaitIdle(context_->device);
        if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(context_->device, sampler_, nullptr);
        if (view_ != VK_NULL_HANDLE) vkDestroyImageView(context_->device, view_, nullptr);
        // A wrapped external image is not owned: leave its image/memory intact.
        if (ownsImage_) {
            if (image_ != VK_NULL_HANDLE) vkDestroyImage(context_->device, image_, nullptr);
            if (memory_ != VK_NULL_HANDLE) vkFreeMemory(context_->device, memory_, nullptr);
        }
        if (context_->imageTextureCount > 0) {
            --context_->imageTextureCount;
        }
    }

    bool isValid() const override
    {
        return image_ != VK_NULL_HANDLE && view_ != VK_NULL_HANDLE && sampler_ != VK_NULL_HANDLE;
    }
    ImageAlphaType alphaType() const override { return alphaType_; }

    void bind(const RenderContext & /*context*/) const override {}

    bool updateRGBA(int x, int y, int width, int height, const unsigned char *pixels,
                    bool /*regenerateMipmaps*/) override
    {
        if (!isValid() || pixels == nullptr || width <= 0 || height <= 0 || x < 0 || y < 0 || x + width > width_
            || y + height > height_) {
            return false;
        }
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        if (!context_->createHostVisibleBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, stagingMemory)) {
            return false;
        }
        void *mapped = nullptr;
        if (vkMapMemory(context_->device, stagingMemory, 0, bytes, 0, &mapped) != VK_SUCCESS) {
            vkDestroyBuffer(context_->device, staging, nullptr);
            vkFreeMemory(context_->device, stagingMemory, nullptr);
            return false;
        }
        std::memcpy(mapped, pixels, static_cast<std::size_t>(bytes));
        vkUnmapMemory(context_->device, stagingMemory);

        VkCommandBuffer cmd = context_->beginSingleTimeCommands();
        bool ok = cmd != VK_NULL_HANDLE;
        if (ok) {
            VulkanRenderDevice::VulkanContext::transitionImageLayout(cmd, image_,
                                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            context_->copyBufferToImage(cmd, staging, image_, x, y, width, height);
            VulkanRenderDevice::VulkanContext::transitionImageLayout(cmd, image_,
                                                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            ok = context_->endSingleTimeCommands(cmd);
        }
        vkDestroyBuffer(context_->device, staging, nullptr);
        vkFreeMemory(context_->device, stagingMemory, nullptr);
        return ok;
    }

    VkImageView view() const { return view_; }
    VkSampler sampler() const { return sampler_; }
    int mipLevels() const { return mipLevels_; }
    VkImage image() const { return image_; }
    int width() const { return width_; }
    int height() const { return height_; }
    VulkanRenderDevice::VulkanContext *context() const { return context_; }

private:
    VulkanRenderDevice::VulkanContext *context_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    int width_ = 0;
    int height_ = 0;
    int mipLevels_ = 1;
    bool ownsImage_ = true;
    ImageAlphaType alphaType_ = ImageAlphaType::Straight;
};

// Create an owning sampled RGBA8 texture from tightly-packed pixels.
std::shared_ptr<VulkanTextureResource> createSampledTexture(VulkanRenderDevice::VulkanContext *ctx, int width,
                                                            int height, const unsigned char *pixels, bool nearest,
                                                            bool generateMips = false){
    if (ctx == nullptr || ctx->device == VK_NULL_HANDLE || width <= 0 || height <= 0 || pixels == nullptr) {
        return nullptr;
    }
    VkDevice device = ctx->device;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    auto cleanup = [&]() {
        if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
        if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
        if (image != VK_NULL_HANDLE) vkDestroyImage(device, image, nullptr);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    };

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kRenderTargetFormat;
    imageInfo.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    const std::uint32_t mipLevels =
        generateMips
            ? (static_cast<std::uint32_t>(std::floor(std::log2(static_cast<double>(std::max(width, height))))) + 1u)
            : 1u;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                      | (generateMips ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0u);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        return nullptr;
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device, image, &memReq);
    const auto typeIndex = ctx->findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!typeIndex.has_value()) {
        cleanup();
        return nullptr;
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = typeIndex.value();
    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }
    vkBindImageMemory(device, image, memory, 0);

    // Upload pixels via a host-visible staging buffer.
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (!ctx->createHostVisibleBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, stagingMemory)) {
        cleanup();
        return nullptr;
    }
    void *mapped = nullptr;
    if (vkMapMemory(device, stagingMemory, 0, bytes, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);
        cleanup();
        return nullptr;
    }
    std::memcpy(mapped, pixels, static_cast<std::size_t>(bytes));
    vkUnmapMemory(device, stagingMemory);

    VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
    bool uploaded = cmd != VK_NULL_HANDLE;
    if (uploaded) {
        // Transition all mip levels UNDEFINED -> TRANSFER_DST, then upload level 0.
        VkImageMemoryBarrier toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.image = image;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toDst.subresourceRange.baseMipLevel = 0;
        toDst.subresourceRange.levelCount = mipLevels;
        toDst.subresourceRange.baseArrayLayer = 0;
        toDst.subresourceRange.layerCount = 1;
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &toDst);
        ctx->copyBufferToImage(cmd, staging, image, 0, 0, width, height);

        auto barrierLevel = [&](std::uint32_t level, VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcA,
                                VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.image = image;
            b.oldLayout = oldL;
            b.newLayout = newL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.baseMipLevel = level;
            b.subresourceRange.levelCount = 1;
            b.subresourceRange.baseArrayLayer = 0;
            b.subresourceRange.layerCount = 1;
            b.srcAccessMask = srcA;
            b.dstAccessMask = dstA;
            vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
        };

        if (mipLevels > 1) {
            std::int32_t mipW = width;
            std::int32_t mipH = height;
            for (std::uint32_t i = 1; i < mipLevels; ++i) {
                // Source level i-1: TRANSFER_DST -> TRANSFER_SRC, then blit to level i.
                barrierLevel(i - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                const std::int32_t nw = mipW > 1 ? mipW / 2 : 1;
                const std::int32_t nh = mipH > 1 ? mipH / 2 : 1;
                VkImageBlit blit{};
                blit.srcOffsets[1] = {mipW, mipH, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i - 1;
                blit.srcSubresource.layerCount = 1;
                blit.dstOffsets[1] = {nw, nh, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.layerCount = 1;
                vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                mipW = nw;
                mipH = nh;
            }
            // Levels 0..mipLevels-2 are TRANSFER_SRC; the last is TRANSFER_DST.
            for (std::uint32_t i = 0; i + 1 < mipLevels; ++i) {
                barrierLevel(i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            }
            barrierLevel(mipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        } else {
            barrierLevel(0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
        uploaded = ctx->endSingleTimeCommands(cmd);
    }
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    if (!uploaded) {
        cleanup();
        return nullptr;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kRenderTargetFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    samplerInfo.minFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = generateMips ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = generateMips ? static_cast<float>(mipLevels) : 0.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }

    return std::make_shared<VulkanTextureResource>(ctx, image, memory, view, sampler, width, height,
                                                   static_cast<int>(mipLevels));
}

std::shared_ptr<VulkanTextureResource> createEmptySampledTexture(
    VulkanRenderDevice::VulkanContext *ctx, int width, int height,
    ImageAlphaType alphaType = ImageAlphaType::Straight)
{
    if (ctx == nullptr || ctx->device == VK_NULL_HANDLE || width <= 0 || height <= 0) {
        return nullptr;
    }

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    auto cleanup = [&]() {
        if (sampler != VK_NULL_HANDLE) vkDestroySampler(ctx->device, sampler, nullptr);
        if (view != VK_NULL_HANDLE) vkDestroyImageView(ctx->device, view, nullptr);
        if (image != VK_NULL_HANDLE) vkDestroyImage(ctx->device, image, nullptr);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(ctx->device, memory, nullptr);
    };

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kRenderTargetFormat;
    imageInfo.extent = {static_cast<std::uint32_t>(width),
                        static_cast<std::uint32_t>(height), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(ctx->device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        return nullptr;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(ctx->device, image, &requirements);
    const auto typeIndex =
        ctx->findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!typeIndex.has_value()) {
        cleanup();
        return nullptr;
    }
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = typeIndex.value();
    if (vkAllocateMemory(ctx->device, &allocation, nullptr, &memory) != VK_SUCCESS
        || vkBindImageMemory(ctx->device, image, memory, 0) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kRenderTargetFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(ctx->device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(ctx->device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }

    return std::make_shared<VulkanTextureResource>(
        ctx, image, memory, view, sampler, width, height, 1, true, alphaType);
}

// Clip-mask resource holding the analytic-AA path data. Application into a live
// pass is done by the device's coverage-mask draw path.
class VulkanClipMaskResource final : public ClipMaskResource
{
public:
    explicit VulkanClipMaskResource(const ClipMaskPath &path)
        : points_(path.points), coverage_(path.coverage), transform_(path.transform)
    {
    }

    bool isValid() const override { return !points_.empty(); }
    void apply(const RenderContext & /*context*/, const ScissorState & /*scissor*/,
               std::size_t /*clipIndex*/) const override
    {
        // Application through the shared RenderContext is pending the
        // backend-neutral command layer; the Vulkan coverage-mask mechanism is
        // exercised directly via VulkanRenderDevice::renderClippedSolid().
    }

    const std::vector<float> &points() const { return points_; }
    const std::vector<float> &coverage() const { return coverage_; }
    const glm::mat4 &transform() const { return transform_; }

private:
    std::vector<float> points_;
    std::vector<float> coverage_;
    glm::mat4 transform_ = glm::mat4(1.0f);
};

// Offscreen render target: color image + view + clear render pass + framebuffer.
class VulkanRenderTarget final : public IRenderTarget
{
public:
    VulkanRenderTarget(VulkanRenderDevice::VulkanContext *context, int width, int height, VkImage image,
                       VkDeviceMemory memory, VkImageView view, VkRenderPass renderPass, VkFramebuffer framebuffer,
                       bool ownsImage = true)
        : context_(context),
          width_(width),
          height_(height),
          image_(image),
          memory_(memory),
          view_(view),
          renderPass_(renderPass),
          framebuffer_(framebuffer),
          ownsImage_(ownsImage),
          imageResource_(std::make_shared<VulkanImageResource>(image, width, height))
    {
        if (context_) {
            ++context_->renderTargetCount;
        }
    }

    ~VulkanRenderTarget() override
    {
        if (!context_ || context_->device == VK_NULL_HANDLE) {
            return;
        }
        if (!synchronized_) {
            vkDeviceWaitIdle(context_->device);
        }
        if (context_->readbackImage == image_) {
            context_->readbackImage = VK_NULL_HANDLE;
            context_->readbackLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            context_->readbackWidth = 0;
            context_->readbackHeight = 0;
        }
        if (framebuffer_ != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(context_->device, framebuffer_, nullptr);
        }
        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(context_->device, renderPass_, nullptr);
        }
        if (view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(context_->device, view_, nullptr);
        }
        if (ownsImage_ && image_ != VK_NULL_HANDLE) {
            vkDestroyImage(context_->device, image_, nullptr);
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(context_->device, memory_, nullptr);
        }
        if (context_->renderTargetCount > 0) {
            --context_->renderTargetCount;
        }
    }

    bool isValid() const override
    {
        return width_ > 0 && height_ > 0 && image_ != VK_NULL_HANDLE && view_ != VK_NULL_HANDLE
            && renderPass_ != VK_NULL_HANDLE && framebuffer_ != VK_NULL_HANDLE;
    }

    int width() const override { return width_; }
    int height() const override { return height_; }

    bool begin(const OffscreenRenderRequest &request) override
    {
        request_ = request;
        begun_ = true;
        activated_ = false;
        return isValid();
    }

    void activate() override
    {
        if (activated_ || !begun_ || !isValid()) {
            return;
        }

        VkCommandBuffer cmd = context_->beginSingleTimeCommands();
        if (cmd == VK_NULL_HANDLE) {
            return;
        }

        VkClearValue clearValue{};
        clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass_;
        renderPassInfo.framebuffer = framebuffer_;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_)};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        // No draws yet (M3); the clear load-op paints the target.
        vkCmdEndRenderPass(cmd);
        context_->endSingleTimeCommands(cmd);

        // The render pass finalLayout leaves the image ready for a transfer read.
        layout_ = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        context_->readbackImage = image_;
        context_->readbackLayout = layout_;
        context_->readbackWidth = width_;
        context_->readbackHeight = height_;
        activated_ = true;
    }

    bool isActivated() const override { return activated_; }

    void end() override
    {
        begun_ = false;
        activated_ = false;
    }

    SharedImageResource getImageResource() const override { return imageResource_; }

    VkImage image() const { return image_; }
    VkImageView view() const { return view_; }
    VkImageLayout layout() const { return layout_; }
    void setLayout(VkImageLayout layout) { layout_ = layout; }
    VkRenderPass renderPass() const { return renderPass_; }
    VkFramebuffer framebuffer() const { return framebuffer_; }
    void markReadbackReady() { activated_ = true; }
    void markSynchronized() { synchronized_ = true; }

private:
    VulkanRenderDevice::VulkanContext *context_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    bool ownsImage_ = true;
    bool synchronized_ = false;
    SharedImageResource imageResource_;
    bool begun_ = false;
    bool activated_ = false;
    OffscreenRenderRequest request_{};
};

// A single solid draw within a render pass: interleaved {x,y,r,g,b,a} vertices.
struct SolidBatch
{
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    int blendMode = 0;
    std::vector<float> vertices;
    std::uint32_t vertexCount = 0;
};

// Record and submit one or more solid draws into `rt` inside a single render
// pass (loadOp CLEAR). Multiple batches enable draw-over-draw compositing (e.g.
// blend-mode validation). Leaves the target ready for readback.
bool recordSolidBatches(VulkanRenderDevice::VulkanContext *ctx, VulkanRenderTarget *rt,
                        const std::vector<SolidBatch> &batches)
{
    if (ctx == nullptr || rt == nullptr || !rt->isValid() || batches.empty()) {
        return false;
    }

    std::vector<VkPipeline> pipelines(batches.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < batches.size(); ++i) {
        pipelines[i] = ctx->ensureSolidPipeline(rt->renderPass(), batches[i].topology, batches[i].blendMode);
        if (pipelines[i] == VK_NULL_HANDLE) {
            return false;
        }
    }

    std::vector<VkBuffer> buffers(batches.size(), VK_NULL_HANDLE);
    std::vector<VkDeviceMemory> memories(batches.size(), VK_NULL_HANDLE);
    auto destroyBuffers = [&]() {
        for (std::size_t i = 0; i < batches.size(); ++i) {
            if (buffers[i] != VK_NULL_HANDLE) vkDestroyBuffer(ctx->device, buffers[i], nullptr);
            if (memories[i] != VK_NULL_HANDLE) vkFreeMemory(ctx->device, memories[i], nullptr);
        }
    };

    for (std::size_t i = 0; i < batches.size(); ++i) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(batches[i].vertices.size()) * sizeof(float);
        if (!ctx->createHostVisibleBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, buffers[i], memories[i])) {
            destroyBuffers();
            return false;
        }
        void *mapped = nullptr;
        if (vkMapMemory(ctx->device, memories[i], 0, bytes, 0, &mapped) != VK_SUCCESS) {
            destroyBuffers();
            return false;
        }
        std::memcpy(mapped, batches[i].vertices.data(), static_cast<std::size_t>(bytes));
        vkUnmapMemory(ctx->device, memories[i]);
    }

    VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) {
        destroyBuffers();
        return false;
    }

    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = rt->renderPass();
    renderPassInfo.framebuffer = rt->framebuffer();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {static_cast<std::uint32_t>(rt->width()),
                                        static_cast<std::uint32_t>(rt->height())};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(rt->width());
    viewport.height = static_cast<float>(rt->height());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = {static_cast<std::uint32_t>(rt->width()), static_cast<std::uint32_t>(rt->height())};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const VkDeviceSize offset = 0;
    for (std::size_t i = 0; i < batches.size(); ++i) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[i]);
        vkCmdBindVertexBuffers(cmd, 0, 1, &buffers[i], &offset);
        vkCmdDraw(cmd, batches[i].vertexCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    const bool submitted = ctx->endSingleTimeCommands(cmd);

    destroyBuffers();

    if (!submitted) {
        return false;
    }

    rt->setLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    rt->markReadbackReady();
    ctx->readbackImage = rt->image();
    ctx->readbackLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ctx->readbackWidth = rt->width();
    ctx->readbackHeight = rt->height();
    return true;
}

// Interleave positions + a single solid color into a batch vertex array
// (x, y, r, g, b, a, coverage=1).
std::vector<float> buildSolidVertices(const std::vector<float> &ndcPositions, float r, float g, float b, float a)
{
    const std::size_t vertexCount = ndcPositions.size() / 2;
    std::vector<float> out;
    out.reserve(vertexCount * 7);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        out.push_back(ndcPositions[i * 2 + 0]);
        out.push_back(ndcPositions[i * 2 + 1]);
        out.push_back(r);
        out.push_back(g);
        out.push_back(b);
        out.push_back(a);
        out.push_back(1.0f);
    }
    return out;
}

// A full-target quad as a triangle list (6 vertices) in NDC.
const std::vector<float> &fullTargetQuad()
{
    static const std::vector<float> kQuad = {
        -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 1.0f,  1.0f, -1.0f, 1.0f,
    };
    return kQuad;
}

struct FilterPassRecording
{
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    void destroy(VkDevice device)
    {
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
            descriptorSet = VK_NULL_HANDLE;
        }
        if (uniformBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, uniformBuffer, nullptr);
            uniformBuffer = VK_NULL_HANDLE;
        }
        if (uniformMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, uniformMemory, nullptr);
            uniformMemory = VK_NULL_HANDLE;
        }
    }
};

bool prepareFilterPass(VulkanRenderDevice::VulkanContext *ctx,
                       VulkanRenderTarget *destination,
                       VkImageView sourceView,
                       VkSampler sourceSampler,
                       const wsc::render::GaussianKernel &kernel,
                       float directionX, float directionY,
                       bool decal,
                       float saturation, float brightness, float contrast,
                       float grain, bool sourcePremultiplied,
                       bool outputStraight, bool resampleStraightAlpha,
                       FilterPassRecording &recording)
{
    if (ctx == nullptr || destination == nullptr || !destination->isValid()
        || sourceView == VK_NULL_HANDLE || sourceSampler == VK_NULL_HANDLE
        || kernel.weights.empty() || kernel.radius() > 64) {
        return false;
    }
    VkPipeline pipeline = ctx->ensureFilterPipeline(destination->renderPass());
    if (pipeline == VK_NULL_HANDLE) {
        return false;
    }

    FilterUBO uniform;
    uniform.directionRadiusDecal[0] = directionX;
    uniform.directionRadiusDecal[1] = directionY;
    uniform.directionRadiusDecal[2] = static_cast<float>(kernel.radius());
    uniform.directionRadiusDecal[3] = decal ? 1.0f : 0.0f;
    uniform.colorAdjustment[0] = saturation;
    uniform.colorAdjustment[1] = brightness;
    uniform.colorAdjustment[2] = contrast;
    uniform.colorAdjustment[3] = grain;
    uniform.options[0] =
        std::abs(saturation - 1.0f) > 1e-6f
        || std::abs(brightness - 1.0f) > 1e-6f
        || std::abs(contrast - 1.0f) > 1e-6f
        ? 1.0f : 0.0f;
    uniform.options[1] = sourcePremultiplied ? 1.0f : 0.0f;
    uniform.options[2] = outputStraight ? 1.0f : 0.0f;
    uniform.options[3] = resampleStraightAlpha ? 1.0f : 0.0f;
    std::copy(kernel.weights.begin(), kernel.weights.end(), uniform.packedWeights);

    VkDevice device = ctx->device;
    if (!ctx->createHostVisibleBuffer(
            sizeof(FilterUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            recording.uniformBuffer, recording.uniformMemory)) {
        recording.destroy(device);
        return false;
    }

    void *mapped = nullptr;
    if (vkMapMemory(device, recording.uniformMemory, 0, sizeof(FilterUBO), 0, &mapped)
        != VK_SUCCESS) {
        recording.destroy(device);
        return false;
    }
    std::memcpy(mapped, &uniform, sizeof(FilterUBO));
    vkUnmapMemory(device, recording.uniformMemory);

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &recording.descriptorPool)
        != VK_SUCCESS) {
        recording.destroy(device);
        return false;
    }

    VkDescriptorSetAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocation.descriptorPool = recording.descriptorPool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &ctx->filterDescriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocation, &recording.descriptorSet)
        != VK_SUCCESS) {
        recording.destroy(device);
        return false;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sourceSampler;
    imageInfo.imageView = sourceView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = recording.uniformBuffer;
    bufferInfo.range = sizeof(FilterUBO);
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = recording.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &imageInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = recording.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);

    return true;
}

void recordFilterPass(VulkanRenderDevice::VulkanContext *ctx,
                      VkCommandBuffer commandBuffer,
                      VulkanRenderTarget *destination,
                      const FilterPassRecording &recording)
{
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = destination->renderPass();
    passInfo.framebuffer = destination->framebuffer();
    passInfo.renderArea.extent = {
        static_cast<std::uint32_t>(destination->width()),
        static_cast<std::uint32_t>(destination->height())};
    passInfo.clearValueCount = 1;
    passInfo.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(destination->width());
    viewport.height = static_cast<float>(destination->height());
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = {
        static_cast<std::uint32_t>(destination->width()),
        static_cast<std::uint32_t>(destination->height())};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      ctx->filterPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ctx->filterPipelineLayout, 0, 1,
                            &recording.descriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
}

SharedImageResource copyRenderTargetToSampledTexture(
    VulkanRenderDevice::VulkanContext *ctx, VulkanRenderTarget *source)
{
    if (ctx == nullptr || source == nullptr || !source->isValid()) {
        return {};
    }
    auto destination = createEmptySampledTexture(
        ctx, source->width(), source->height(),
        ImageAlphaType::Premultiplied);
    if (!destination) {
        return {};
    }

    VkCommandBuffer commandBuffer = ctx->beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) {
        return {};
    }
    VulkanRenderDevice::VulkanContext::transitionImageLayout(
        commandBuffer, destination->image(), VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    // A layout value alone is not a memory dependency. Issue the barrier even
    // when the render pass already selected TRANSFER_SRC_OPTIMAL so its color
    // writes are visible to this transfer read.
    VulkanRenderDevice::VulkanContext::transitionImageLayout(
        commandBuffer, source->image(), source->layout(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.extent = {static_cast<std::uint32_t>(source->width()),
                     static_cast<std::uint32_t>(source->height()), 1};
    vkCmdCopyImage(commandBuffer,
                   source->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   destination->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &region);
    VulkanRenderDevice::VulkanContext::transitionImageLayout(
        commandBuffer, destination->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!ctx->endSingleTimeCommands(commandBuffer)) {
        return {};
    }
    source->setLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    return destination;
}

// Locate a queue family that supports graphics operations.
std::optional<std::uint32_t> findGraphicsQueueFamily(VkPhysicalDevice device)
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    if (count == 0) {
        return std::nullopt;
    }

    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (std::uint32_t i = 0; i < count; ++i) {
        if (families[i].queueCount > 0 && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            return i;
        }
    }
    return std::nullopt;
}

// Score a physical device so discrete GPUs are preferred over integrated ones.
int scorePhysicalDevice(const VkPhysicalDeviceProperties &props)
{
    int score = 0;
    switch (props.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        score += 1000;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        score += 250;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        score += 100;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        score += 10;
        break;
    default:
        break;
    }
    score += static_cast<int>(props.limits.maxImageDimension2D / 16);
    return score;
}

} // namespace

#endif // WHATSCANVAS_ENABLE_VULKAN

#if !defined(WHATSCANVAS_ENABLE_VULKAN)

// Empty context so std::unique_ptr<VulkanContext> has a complete type even when
// Vulkan support is not compiled into this build.
struct VulkanRenderDevice::VulkanContext
{
};

#endif // !WHATSCANVAS_ENABLE_VULKAN

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

VulkanRenderDevice::VulkanRenderDevice()
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    : context_(std::make_unique<VulkanContext>())
#else
    : context_(nullptr)
#endif
{
}

VulkanRenderDevice::~VulkanRenderDevice()
{
    finalizeBackend();
}

// ---------------------------------------------------------------------------
// Backend bring-up
// ---------------------------------------------------------------------------

void VulkanRenderDevice::initializeBackend()
{
    if (backendInitialized_) {
        return;
    }

#if defined(WHATSCANVAS_ENABLE_VULKAN)
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "WhatsCanvas";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "WhatsCanvas";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    // Enable surface extensions when available so the device can present to a
    // window; their absence keeps the instance headless (offscreen only).
    std::vector<const char *> instanceExtensions;
    {
        std::uint32_t count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available(count);
        if (count > 0) {
            vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
        }
        auto has = [&](const char *name) {
            for (const auto &e : available) {
                if (std::strcmp(e.extensionName, name) == 0) {
                    return true;
                }
            }
            return false;
        };
#if defined(_WIN32)
        if (has(VK_KHR_SURFACE_EXTENSION_NAME) && has(VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
            instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
            instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
        }
#endif
    }
    const bool instanceHasSurface = !instanceExtensions.empty();

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data();

    if (vkCreateInstance(&instanceInfo, nullptr, &context_->instance) != VK_SUCCESS) {
        WSC_LOG_ERROR("VulkanRenderDevice", "Failed to create Vulkan instance.");
        return;
    }

    std::uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(context_->instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        WSC_LOG_ERROR("VulkanRenderDevice", "No Vulkan-capable physical devices found.");
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(context_->instance, &deviceCount, devices.data());

    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    std::optional<std::uint32_t> bestQueueFamily;
    VkPhysicalDeviceProperties bestProps{};

    for (VkPhysicalDevice candidate : devices) {
        const auto queueFamily = findGraphicsQueueFamily(candidate);
        if (!queueFamily.has_value()) {
            continue;
        }

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(candidate, &props);
        const int score = scorePhysicalDevice(props);
        if (score > bestScore) {
            bestScore = score;
            bestDevice = candidate;
            bestQueueFamily = queueFamily;
            bestProps = props;
        }
    }

    if (bestDevice == VK_NULL_HANDLE || !bestQueueFamily.has_value()) {
        WSC_LOG_ERROR("VulkanRenderDevice", "No physical device with a graphics queue was found.");
        return;
    }

    context_->physicalDevice = bestDevice;
    context_->graphicsQueueFamily = bestQueueFamily.value();
    context_->physicalDeviceName = bestProps.deviceName;

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = context_->graphicsQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};

    // Enable the swapchain extension when the instance is surface-capable and the
    // device supports it, so this device can present to a window.
    std::vector<const char *> deviceExtensions;
    bool deviceHasSwapchain = false;
    if (instanceHasSurface) {
        std::uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(context_->physicalDevice, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available(count);
        if (count > 0) {
            vkEnumerateDeviceExtensionProperties(context_->physicalDevice, nullptr, &count, available.data());
        }
        for (const auto &e : available) {
            if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                deviceHasSwapchain = true;
                break;
            }
        }
        if (deviceHasSwapchain) {
            deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }
    }

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.pEnabledFeatures = &deviceFeatures;
    deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data();

    if (vkCreateDevice(context_->physicalDevice, &deviceInfo, nullptr, &context_->device) != VK_SUCCESS) {
        WSC_LOG_ERROR("VulkanRenderDevice", "Failed to create Vulkan logical device.");
        return;
    }

    vkGetDeviceQueue(context_->device, context_->graphicsQueueFamily, 0, &context_->graphicsQueue);

    // M1 render core: command pool for single-time and per-frame submissions.
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context_->graphicsQueueFamily;
    if (vkCreateCommandPool(context_->device, &poolInfo, nullptr, &context_->commandPool) != VK_SUCCESS) {
        WSC_LOG_ERROR("VulkanRenderDevice", "Failed to create Vulkan command pool.");
        return;
    }

    context_->deviceReady = true;
    context_->presentCapable = instanceHasSurface && deviceHasSwapchain;
    backendInitialized_ = true;

    WSC_LOG_INFO("VulkanRenderDevice", "Initialized on device: " << context_->physicalDeviceName);
#else
    WSC_LOG_ERROR("VulkanRenderDevice", "Vulkan support is not compiled into this build. "
                                        "Reconfigure with -DWHATSCANVAS_ENABLE_VULKAN=ON and a Vulkan SDK to enable it.");
#endif
}

void VulkanRenderDevice::finalizeBackend()
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (context_) {
        if (context_->device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(context_->device);
        }
        // Recreate a fresh, empty context so the device can be re-initialized.
        context_ = std::make_unique<VulkanContext>();
    }
#endif
    backendInitialized_ = false;
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

bool VulkanRenderDevice::isDeviceReady() const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    return context_ && context_->deviceReady;
#else
    return false;
#endif
}

#if defined(WHATSCANVAS_ENABLE_VULKAN)

// Presents the render device's most-recently-rendered offscreen image
// (context_->readbackImage) to a window by blitting it into the acquired
// swapchain image. Reuses the entire existing Vulkan renderer; only adds the
// surface/swapchain and a per-frame blit.
class VulkanRenderDevice::VulkanSwapchain : public ISwapchain
{
public:
    VulkanSwapchain(VulkanRenderDevice *owner, VkSurfaceKHR surface) : owner_(owner), surface_(surface)
    {
        createSyncAndCommandBuffer();
        recreate();
    }

    ~VulkanSwapchain() override { destroy(); }

    bool valid() const { return swapchain_ != VK_NULL_HANDLE; }

    AcquiredImage acquire() override
    {
        return AcquiredImage{nullptr, static_cast<int>(extent_.width), static_cast<int>(extent_.height), valid()};
    }

    bool present() override { return presentFrame(); }

    void resize(int /*width*/, int /*height*/) override { recreate(); }

private:
    VulkanContext *ctx() const { return owner_->context_.get(); }

    static void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldL, VkImageLayout newL,
                        VkAccessFlags srcA, VkAccessFlags dstA, VkPipelineStageFlags srcS,
                        VkPipelineStageFlags dstS)
    {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        b.srcAccessMask = srcA;
        b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    void createSyncAndCommandBuffer()
    {
        VkDevice dev = ctx()->device;
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(dev, &si, nullptr, &imageAvailable_);
        vkCreateSemaphore(dev, &si, nullptr, &renderFinished_);
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(dev, &fi, nullptr, &inFlight_);
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = ctx()->commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        vkAllocateCommandBuffers(dev, &ai, &cmd_);
    }

    void recreate()
    {
        VulkanContext *c = ctx();
        VkDevice dev = c->device;
        vkDeviceWaitIdle(dev);
        VkSwapchainKHR old = swapchain_;

        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c->physicalDevice, surface_, &caps);

        std::uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(c->physicalDevice, surface_, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        if (formatCount > 0) {
            vkGetPhysicalDeviceSurfaceFormatsKHR(c->physicalDevice, surface_, &formatCount, formats.data());
        }
        VkSurfaceFormatKHR chosen{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        if (!formats.empty()) {
            chosen = formats[0];
            for (const auto &f : formats) {
                if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    chosen = f;
                    break;
                }
            }
        }
        format_ = chosen.format;

        extent_ = caps.currentExtent;
        if (extent_.width == 0xFFFFFFFFu) {
            extent_ = caps.minImageExtent;
        }
        if (extent_.width == 0 || extent_.height == 0) {
            swapchain_ = VK_NULL_HANDLE;
            if (old != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(dev, old, nullptr);
            }
            return;
        }

        std::uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
            imageCount = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface_;
        info.minImageCount = imageCount;
        info.imageFormat = format_;
        info.imageColorSpace = chosen.colorSpace;
        info.imageExtent = extent_;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        info.oldSwapchain = old;

        VkSwapchainKHR created = VK_NULL_HANDLE;
        const VkResult r = vkCreateSwapchainKHR(dev, &info, nullptr, &created);
        if (old != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(dev, old, nullptr);
        }
        if (r != VK_SUCCESS) {
            WSC_LOG_ERROR("VulkanRenderDevice", "vkCreateSwapchainKHR failed.");
            swapchain_ = VK_NULL_HANDLE;
            return;
        }
        swapchain_ = created;

        std::uint32_t count = 0;
        vkGetSwapchainImagesKHR(dev, swapchain_, &count, nullptr);
        images_.resize(count);
        vkGetSwapchainImagesKHR(dev, swapchain_, &count, images_.data());
    }

    bool presentFrame()
    {
        if (!valid()) {
            recreate();
            if (!valid()) {
                return false;
            }
        }
        VulkanContext *c = ctx();
        if (c->readbackImage == VK_NULL_HANDLE || c->readbackWidth <= 0 || c->readbackHeight <= 0) {
            return false;
        }
        VkDevice dev = c->device;

        vkWaitForFences(dev, 1, &inFlight_, VK_TRUE, UINT64_MAX);

        std::uint32_t idx = 0;
        const VkResult acq = vkAcquireNextImageKHR(dev, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &idx);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate();
            return false;
        }
        if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
            return false;
        }

        vkResetFences(dev, 1, &inFlight_);
        vkResetCommandBuffer(cmd_, 0);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_, &bi);

        VkImage dst = images_[idx];
        VkImage src = c->readbackImage;
        const VkImageLayout srcLayout = c->readbackLayout;

        barrier(cmd_, src, srcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        barrier(cmd_, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkImageBlit region{};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.layerCount = 1;
        region.srcOffsets[1] = VkOffset3D{c->readbackWidth, c->readbackHeight, 1};
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.layerCount = 1;
        region.dstOffsets[1] = VkOffset3D{static_cast<int>(extent_.width), static_cast<int>(extent_.height), 1};
        vkCmdBlitImage(cmd_, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                       &region, VK_FILTER_LINEAR);

        barrier(cmd_, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        barrier(cmd_, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcLayout, VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

        vkEndCommandBuffer(cmd_);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &imageAvailable_;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd_;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &renderFinished_;
        if (vkQueueSubmit(c->graphicsQueue, 1, &submit, inFlight_) != VK_SUCCESS) {
            return false;
        }

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderFinished_;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &idx;
        const VkResult pr = vkQueuePresentKHR(c->graphicsQueue, &present);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
            recreate();
        } else if (pr != VK_SUCCESS) {
            return false;
        }
        return true;
    }

    void destroy()
    {
        if (owner_ == nullptr || !owner_->context_) {
            return;
        }
        VkDevice dev = owner_->context_->device;
        if (dev != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(dev);
            if (cmd_ != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(dev, owner_->context_->commandPool, 1, &cmd_);
            }
            if (inFlight_ != VK_NULL_HANDLE) {
                vkDestroyFence(dev, inFlight_, nullptr);
            }
            if (renderFinished_ != VK_NULL_HANDLE) {
                vkDestroySemaphore(dev, renderFinished_, nullptr);
            }
            if (imageAvailable_ != VK_NULL_HANDLE) {
                vkDestroySemaphore(dev, imageAvailable_, nullptr);
            }
            if (swapchain_ != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(dev, swapchain_, nullptr);
            }
        }
        if (surface_ != VK_NULL_HANDLE && owner_->context_->instance != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(owner_->context_->instance, surface_, nullptr);
        }
    }

    VulkanRenderDevice *owner_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence inFlight_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
};

bool VulkanRenderDevice::supportsPresentation() const
{
    return isDeviceReady() && context_ && context_->presentCapable;
}

std::unique_ptr<ISwapchain> VulkanRenderDevice::createSwapchain(const NativeSurface &surface,
                                                                const SwapchainConfig & /*config*/)
{
    if (!isDeviceReady() || !context_ || !context_->presentCapable) {
        WSC_LOG_WARN("VulkanRenderDevice",
                     "Vulkan present unavailable (headless instance/device or unsupported).");
        return nullptr;
    }
#if defined(_WIN32)
    if (surface.platform != NativeSurface::Platform::Win32 || surface.window == nullptr) {
        WSC_LOG_WARN("VulkanRenderDevice", "Vulkan present requires a Win32 window handle.");
        return nullptr;
    }
    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = GetModuleHandle(nullptr);
    surfaceInfo.hwnd = static_cast<HWND>(surface.window);
    VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
    if (vkCreateWin32SurfaceKHR(context_->instance, &surfaceInfo, nullptr, &vkSurface) != VK_SUCCESS) {
        WSC_LOG_ERROR("VulkanRenderDevice", "vkCreateWin32SurfaceKHR failed.");
        return nullptr;
    }
    VkBool32 presentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(context_->physicalDevice, context_->graphicsQueueFamily, vkSurface,
                                         &presentSupported);
    if (presentSupported != VK_TRUE) {
        WSC_LOG_ERROR("VulkanRenderDevice", "Graphics queue cannot present to this surface.");
        vkDestroySurfaceKHR(context_->instance, vkSurface, nullptr);
        return nullptr;
    }
    auto swapchain = std::make_unique<VulkanSwapchain>(this, vkSurface);
    if (!swapchain->valid()) {
        return nullptr;
    }
    return swapchain;
#else
    (void)surface;
    WSC_LOG_WARN("VulkanRenderDevice", "Vulkan present is only wired for Win32 in this build.");
    return nullptr;
#endif
}

bool VulkanRenderDevice::wrapBackendRenderTarget(const BackendRenderTarget &target)
{
    if (!context_ || !context_->deviceReady) {
        return false;
    }
    if (target.kind == BackendRenderTarget::Kind::None) {
        context_->externalRenderTarget.reset();
        return true;
    }
    if (target.kind != BackendRenderTarget::Kind::VulkanImage || target.nativeHandle == nullptr ||
        target.width <= 0 || target.height <= 0) {
        WSC_LOG_WARN("VulkanRenderDevice", "wrapBackendRenderTarget requires a VulkanImage target.");
        return false;
    }

    VkDevice device = context_->device;
    VkImage image = reinterpret_cast<VkImage>(target.nativeHandle);
    VkImageView view = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    auto cleanup = [&]() {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, nullptr);
        if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
        if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
    };

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kRenderTargetFormat; // host image must be R8G8B8A8_UNORM
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        WSC_LOG_ERROR("VulkanRenderDevice", "wrapBackendRenderTarget: vkCreateImageView failed.");
        return false;
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = kRenderTargetFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        cleanup();
        WSC_LOG_ERROR("VulkanRenderDevice", "wrapBackendRenderTarget: vkCreateRenderPass failed.");
        return false;
    }

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &view;
    framebufferInfo.width = static_cast<std::uint32_t>(target.width);
    framebufferInfo.height = static_cast<std::uint32_t>(target.height);
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        cleanup();
        WSC_LOG_ERROR("VulkanRenderDevice", "wrapBackendRenderTarget: vkCreateFramebuffer failed.");
        return false;
    }

    context_->externalRenderTarget = std::make_unique<VulkanRenderTarget>(
        context_.get(), target.width, target.height, image, VK_NULL_HANDLE, view, renderPass, framebuffer,
        /*ownsImage=*/false);
    return true;
}

std::uintptr_t VulkanRenderDevice::nativeHandle(int which) const
{
    if (!context_ || !context_->deviceReady) {
        return 0;
    }
    switch (which) {
    case 0:
        return reinterpret_cast<std::uintptr_t>(context_->instance);
    case 1:
        return reinterpret_cast<std::uintptr_t>(context_->physicalDevice);
    case 2:
        return reinterpret_cast<std::uintptr_t>(context_->device);
    case 3:
        return reinterpret_cast<std::uintptr_t>(context_->graphicsQueue);
    case 4:
        return static_cast<std::uintptr_t>(context_->graphicsQueueFamily);
    default:
        return 0;
    }
}

#else // !WHATSCANVAS_ENABLE_VULKAN

bool VulkanRenderDevice::supportsPresentation() const
{
    return false;
}

std::unique_ptr<ISwapchain> VulkanRenderDevice::createSwapchain(const NativeSurface & /*surface*/,
                                                                const SwapchainConfig & /*config*/)
{
    return nullptr;
}

bool VulkanRenderDevice::wrapBackendRenderTarget(const BackendRenderTarget & /*target*/)
{
    return false;
}

std::uintptr_t VulkanRenderDevice::nativeHandle(int /*which*/) const
{
    return 0;
}

#endif

const std::string &VulkanRenderDevice::selectedDeviceName() const
{
    static const std::string kEmpty;
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    return context_ ? context_->physicalDeviceName : kEmpty;
#else
    return kEmpty;
#endif
}

// ---------------------------------------------------------------------------
// M2: offscreen render target + readback
// ---------------------------------------------------------------------------

std::unique_ptr<IRenderTarget> VulkanRenderDevice::createRenderTarget(int width, int height) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || width <= 0 || height <= 0) {
        return nullptr;
    }

    VkDevice device = context_->device;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    auto cleanup = [&]() {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, nullptr);
        if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
        if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
        if (image != VK_NULL_HANDLE) vkDestroyImage(device, image, nullptr);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    };

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kRenderTargetFormat;
    imageInfo.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                      | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        return nullptr;
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device, image, &memReq);
    const auto typeIndex = context_->findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!typeIndex.has_value()) {
        cleanup();
        return nullptr;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = typeIndex.value();
    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }
    vkBindImageMemory(device, image, memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kRenderTargetFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = kRenderTargetFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &view;
    framebufferInfo.width = static_cast<std::uint32_t>(width);
    framebufferInfo.height = static_cast<std::uint32_t>(height);
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }

    return std::make_unique<VulkanRenderTarget>(context_.get(), width, height, image, memory, view, renderPass,
                                                framebuffer);
#else
    (void)width;
    (void)height;
    return nullptr;
#endif
}

bool VulkanRenderDevice::fillRenderTargetSolid(const std::unique_ptr<IRenderTarget> &target, unsigned char r,
                                               unsigned char g, unsigned char b, unsigned char a) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || !target) {
        return false;
    }
    auto *rt = dynamic_cast<VulkanRenderTarget *>(target.get());
    if (rt == nullptr || !rt->isValid()) {
        return false;
    }

    VkCommandBuffer cmd = context_->beginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) {
        return false;
    }

    VulkanContext::transitionImageLayout(cmd, rt->image(), rt->layout(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkClearColorValue clearColor{};
    clearColor.float32[0] = static_cast<float>(r) / 255.0f;
    clearColor.float32[1] = static_cast<float>(g) / 255.0f;
    clearColor.float32[2] = static_cast<float>(b) / 255.0f;
    clearColor.float32[3] = static_cast<float>(a) / 255.0f;

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;
    vkCmdClearColorImage(cmd, rt->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

    VulkanContext::transitionImageLayout(cmd, rt->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    if (!context_->endSingleTimeCommands(cmd)) {
        return false;
    }

    rt->setLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    context_->readbackImage = rt->image();
    context_->readbackLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    context_->readbackWidth = rt->width();
    context_->readbackHeight = rt->height();
    return true;
#else
    (void)target;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
    return false;
#endif
}

bool VulkanRenderDevice::renderSolidTriangles(const std::unique_ptr<IRenderTarget> &target,
                                              const std::vector<float> &ndcPositions, float r, float g, float b,
                                              float a) const
{
    return renderSolidPrimitives(target, SolidTopology::Triangles, ndcPositions, r, g, b, a);
}

bool VulkanRenderDevice::renderSolidPrimitives(const std::unique_ptr<IRenderTarget> &target, SolidTopology topology,
                                               const std::vector<float> &ndcPositions, float r, float g, float b,
                                               float a) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || !target || ndcPositions.empty() || (ndcPositions.size() % 2) != 0) {
        return false;
    }

    const std::size_t vertexCount = ndcPositions.size() / 2;
    VkPrimitiveTopology vkTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    switch (topology) {
    case SolidTopology::Triangles:
        if (vertexCount < 3 || (vertexCount % 3) != 0) {
            return false;
        }
        vkTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
    case SolidTopology::Lines:
        if (vertexCount < 2 || (vertexCount % 2) != 0) {
            return false;
        }
        vkTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        break;
    case SolidTopology::Points:
        vkTopology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        break;
    }

    auto *rt = dynamic_cast<VulkanRenderTarget *>(target.get());
    if (rt == nullptr) {
        return false;
    }

    SolidBatch batch;
    batch.topology = vkTopology;
    batch.blendMode = 0; // SrcOver
    batch.vertices = buildSolidVertices(ndcPositions, r, g, b, a);
    batch.vertexCount = static_cast<std::uint32_t>(vertexCount);
    return recordSolidBatches(context_.get(), rt, {batch});
#else
    (void)target;
    (void)topology;
    (void)ndcPositions;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
    return false;
#endif
}

bool VulkanRenderDevice::renderGradientTriangles(const std::unique_ptr<IRenderTarget> &target,
                                                 const std::vector<float> &ndcPositions,
                                                 const std::vector<float> &rgbaPerVertex) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || !target || ndcPositions.size() < 6 || (ndcPositions.size() % 6) != 0) {
        return false;
    }
    const std::size_t vertexCount = ndcPositions.size() / 2;
    if (rgbaPerVertex.size() != vertexCount * 4) {
        return false;
    }
    auto *rt = dynamic_cast<VulkanRenderTarget *>(target.get());
    if (rt == nullptr) {
        return false;
    }

    SolidBatch batch;
    batch.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    batch.blendMode = 0; // SrcOver
    batch.vertices.reserve(vertexCount * 7);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        batch.vertices.push_back(ndcPositions[i * 2 + 0]);
        batch.vertices.push_back(ndcPositions[i * 2 + 1]);
        batch.vertices.push_back(rgbaPerVertex[i * 4 + 0]);
        batch.vertices.push_back(rgbaPerVertex[i * 4 + 1]);
        batch.vertices.push_back(rgbaPerVertex[i * 4 + 2]);
        batch.vertices.push_back(rgbaPerVertex[i * 4 + 3]);
        batch.vertices.push_back(1.0f);
    }
    batch.vertexCount = static_cast<std::uint32_t>(vertexCount);
    return recordSolidBatches(context_.get(), rt, {batch});
#else
    (void)target;
    (void)ndcPositions;
    (void)rgbaPerVertex;
    return false;
#endif
}

bool VulkanRenderDevice::renderBlendedOverlay(const std::unique_ptr<IRenderTarget> &target, SolidBlendMode blendMode,
                                              float bgR, float bgG, float bgB, float bgA, float fgR, float fgG,
                                              float fgB, float fgA) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || !target) {
        return false;
    }
    auto *rt = dynamic_cast<VulkanRenderTarget *>(target.get());
    if (rt == nullptr) {
        return false;
    }

    SolidBatch background;
    background.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    background.blendMode = 1; // Src: write background exactly
    background.vertices = buildSolidVertices(fullTargetQuad(), bgR, bgG, bgB, bgA);
    background.vertexCount = 6;

    SolidBatch foreground;
    foreground.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    foreground.blendMode = static_cast<int>(blendMode);
    foreground.vertices = buildSolidVertices(fullTargetQuad(), fgR, fgG, fgB, fgA);
    foreground.vertexCount = 6;

    return recordSolidBatches(context_.get(), rt, {background, foreground});
#else
    (void)target;
    (void)blendMode;
    (void)bgR;
    (void)bgG;
    (void)bgB;
    (void)bgA;
    (void)fgR;
    (void)fgG;
    (void)fgB;
    (void)fgA;
    return false;
#endif
}

bool VulkanRenderDevice::readPixelsRGBA(int width, int height, std::vector<unsigned char> &pixels) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || width <= 0 || height <= 0
        || context_->readbackImage == VK_NULL_HANDLE || width != context_->readbackWidth
        || height != context_->readbackHeight) {
        pixels.clear();
        return false;
    }

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (!context_->createHostVisibleBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, staging, stagingMemory)) {
        pixels.clear();
        return false;
    }

    VkCommandBuffer cmd = context_->beginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) {
        vkDestroyBuffer(context_->device, staging, nullptr);
        vkFreeMemory(context_->device, stagingMemory, nullptr);
        pixels.clear();
        return false;
    }

    if (context_->readbackLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        VulkanContext::transitionImageLayout(cmd, context_->readbackImage, context_->readbackLayout,
                                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    vkCmdCopyImageToBuffer(cmd, context_->readbackImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

    const bool submitted = context_->endSingleTimeCommands(cmd);
    context_->readbackLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    bool success = false;
    if (submitted) {
        void *mapped = nullptr;
        if (vkMapMemory(context_->device, stagingMemory, 0, bufferSize, 0, &mapped) == VK_SUCCESS) {
            pixels.resize(static_cast<std::size_t>(bufferSize));
            std::memcpy(pixels.data(), mapped, static_cast<std::size_t>(bufferSize));
            vkUnmapMemory(context_->device, stagingMemory);
            success = true;
        }
    }

    vkDestroyBuffer(context_->device, staging, nullptr);
    vkFreeMemory(context_->device, stagingMemory, nullptr);

    if (!success) {
        pixels.clear();
    }
    return success;
#else
    (void)width;
    (void)height;
    pixels.clear();
    return false;
#endif
}

RenderResourceStats VulkanRenderDevice::resourceStats() const
{
    RenderResourceStats stats{};
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (context_) {
        stats.renderTargetCount = context_->renderTargetCount;
        stats.imageTextureCount = context_->imageTextureCount;
    }
#endif
    return stats;
}

// ---------------------------------------------------------------------------
// Resource + command entry points (later milestones)
// ---------------------------------------------------------------------------

SharedClipMaskResource VulkanRenderDevice::createClipMaskResource(const ClipMaskPath &maskPath) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (maskPath.points.empty()) {
        return nullptr;
    }
    return std::make_shared<VulkanClipMaskResource>(maskPath);
#else
    (void)maskPath;
    return nullptr;
#endif
}

bool VulkanRenderDevice::renderClippedSolid(const std::unique_ptr<IRenderTarget> &target,
                                            const std::unique_ptr<IRenderTarget> &maskTarget, float r, float g,
                                            float b, float a) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || !target || !maskTarget) {
        return false;
    }
    auto *dst = dynamic_cast<VulkanRenderTarget *>(target.get());
    auto *mask = dynamic_cast<VulkanRenderTarget *>(maskTarget.get());
    if (dst == nullptr || mask == nullptr || !dst->isValid() || !mask->isValid()) {
        return false;
    }
    VkPipeline clipPipe = context_->ensureClipPipeline(dst->renderPass(), /*SrcOver*/ 0);
    if (clipPipe == VK_NULL_HANDLE) {
        return false;
    }

    VkDevice device = context_->device;

    // Temporary view + sampler over the mask image.
    VkImageView maskView = VK_NULL_HANDLE;
    VkSampler maskSampler = VK_NULL_HANDLE;
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = mask->image();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kRenderTargetFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &maskView) != VK_SUCCESS) {
        return false;
    }
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &maskSampler) != VK_SUCCESS) {
        vkDestroyImageView(device, maskView, nullptr);
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    auto cleanup = [&]() {
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
        vkDestroySampler(device, maskSampler, nullptr);
        vkDestroyImageView(device, maskView, nullptr);
    };
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        cleanup();
        return false;
    }
    VkDescriptorSetAllocateInfo setAlloc{};
    setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAlloc.descriptorPool = pool;
    setAlloc.descriptorSetCount = 1;
    setAlloc.pSetLayouts = &context_->clipDescriptorSetLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &setAlloc, &set) != VK_SUCCESS) {
        cleanup();
        return false;
    }
    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescriptor.imageView = maskView;
    imageDescriptor.sampler = maskSampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageDescriptor;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // Full-target quad with color + mask UV (8 floats/vertex, top-left UV).
    const std::array<float, 48> quad = {
        -1.0f, -1.0f, r, g, b, a, 0.0f, 0.0f, 1.0f, -1.0f, r, g, b, a, 1.0f, 0.0f,
        1.0f,  1.0f,  r, g, b, a, 1.0f, 1.0f, -1.0f, -1.0f, r, g, b, a, 0.0f, 0.0f,
        1.0f,  1.0f,  r, g, b, a, 1.0f, 1.0f, -1.0f, 1.0f,  r, g, b, a, 0.0f, 1.0f,
    };
    const VkDeviceSize vertexBytes = quad.size() * sizeof(float);
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    if (!context_->createHostVisibleBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer,
                                           vertexMemory)) {
        cleanup();
        return false;
    }
    void *mapped = nullptr;
    if (vkMapMemory(device, vertexMemory, 0, vertexBytes, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vkFreeMemory(device, vertexMemory, nullptr);
        cleanup();
        return false;
    }
    std::memcpy(mapped, quad.data(), static_cast<std::size_t>(vertexBytes));
    vkUnmapMemory(device, vertexMemory);

    VkCommandBuffer cmd = context_->beginSingleTimeCommands();
    bool submitted = false;
    if (cmd != VK_NULL_HANDLE) {
        VulkanContext::transitionImageLayout(cmd, mask->image(), mask->layout(),
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkClearValue clearValue{};
        clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = dst->renderPass();
        renderPassInfo.framebuffer = dst->framebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {static_cast<std::uint32_t>(dst->width()),
                                            static_cast<std::uint32_t>(dst->height())};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(dst->width());
        viewport.height = static_cast<float>(dst->height());
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = {static_cast<std::uint32_t>(dst->width()), static_cast<std::uint32_t>(dst->height())};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, clipPipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, context_->clipPipelineLayout, 0, 1, &set, 0,
                                nullptr);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
        vkCmdDraw(cmd, 6, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        submitted = context_->endSingleTimeCommands(cmd);
    }

    mask->setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexMemory, nullptr);
    cleanup();

    if (!submitted) {
        return false;
    }
    dst->setLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    dst->markReadbackReady();
    context_->readbackImage = dst->image();
    context_->readbackLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    context_->readbackWidth = dst->width();
    context_->readbackHeight = dst->height();
    return true;
#else
    (void)target;
    (void)maskTarget;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
    return false;
#endif
}

SharedImageResource VulkanRenderDevice::createImageResourceRGBA(int width, int height,
                                                                const std::vector<unsigned char> &pixels) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || width <= 0 || height <= 0
        || pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return nullptr;
    }
    return createSampledTexture(context_.get(), width, height, pixels.data(), /*nearest=*/true);
#else
    (void)width;
    (void)height;
    (void)pixels;
    return nullptr;
#endif
}

SharedImageResource VulkanRenderDevice::createImageResourceFromImageData(int width, int height, int channels,
                                                                         const unsigned char *pixels,
                                                                         bool generateMipmaps) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || width <= 0 || height <= 0 || pixels == nullptr || channels < 1
        || channels > 4) {
        return nullptr;
    }
    // Expand to tightly-packed RGBA8.
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<unsigned char> rgba(pixelCount * 4u, 255);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const unsigned char *src = pixels + i * channels;
        unsigned char *dst = rgba.data() + i * 4u;
        switch (channels) {
        case 1:
            dst[0] = dst[1] = dst[2] = src[0];
            dst[3] = 255;
            break;
        case 2:
            dst[0] = dst[1] = dst[2] = src[0];
            dst[3] = src[1];
            break;
        case 3:
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = 255;
            break;
        default: // 4
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
            break;
        }
    }
    return createSampledTexture(context_.get(), width, height, rgba.data(), /*nearest=*/false,
                                /*generateMips=*/generateMipmaps);
#else
    (void)width;
    (void)height;
    (void)channels;
    (void)pixels;
    return nullptr;
#endif
}

bool VulkanRenderDevice::updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width,
                                                 int height, const unsigned char *pixels,
                                                 bool regenerateMipmaps) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!imageResource) {
        return false;
    }
    return imageResource->updateRGBA(x, y, width, height, pixels, regenerateMipmaps);
#else
    (void)imageResource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)pixels;
    (void)regenerateMipmaps;
    return false;
#endif
}

bool VulkanRenderDevice::renderTexturedQuad(const std::unique_ptr<IRenderTarget> &target,
                                            const SharedImageResource &imageResource) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || !target || !imageResource) {
        return false;
    }
    auto *rt = dynamic_cast<VulkanRenderTarget *>(target.get());
    auto *tex = dynamic_cast<VulkanTextureResource *>(imageResource.get());
    if (rt == nullptr || tex == nullptr || !rt->isValid() || !tex->isValid()) {
        return false;
    }
    VkPipeline texPipe = context_->ensureTexturePipeline(rt->renderPass(), /*SrcOver*/ 0);
    if (texPipe == VK_NULL_HANDLE) {
        return false;
    }

    VkDevice device = context_->device;

    // Descriptor pool + set for this draw (combined image sampler).
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetAllocateInfo setAlloc{};
    setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAlloc.descriptorPool = pool;
    setAlloc.descriptorSetCount = 1;
    setAlloc.pSetLayouts = &context_->texDescriptorSetLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &setAlloc, &set) != VK_SUCCESS) {
        vkDestroyDescriptorPool(device, pool, nullptr);
        return false;
    }

    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescriptor.imageView = tex->view();
    imageDescriptor.sampler = tex->sampler();
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageDescriptor;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // Full-target quad with UVs (top-left origin: NDC (-1,-1) -> UV (0,0)).
    const std::array<float, 24> quad = {
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,
    };
    const VkDeviceSize vertexBytes = quad.size() * sizeof(float);
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    if (!context_->createHostVisibleBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer,
                                           vertexMemory)) {
        vkDestroyDescriptorPool(device, pool, nullptr);
        return false;
    }
    void *mapped = nullptr;
    if (vkMapMemory(device, vertexMemory, 0, vertexBytes, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vkFreeMemory(device, vertexMemory, nullptr);
        vkDestroyDescriptorPool(device, pool, nullptr);
        return false;
    }
    std::memcpy(mapped, quad.data(), static_cast<std::size_t>(vertexBytes));
    vkUnmapMemory(device, vertexMemory);

    VkCommandBuffer cmd = context_->beginSingleTimeCommands();
    bool submitted = false;
    if (cmd != VK_NULL_HANDLE) {
        VkClearValue clearValue{};
        clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = rt->renderPass();
        renderPassInfo.framebuffer = rt->framebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {static_cast<std::uint32_t>(rt->width()),
                                            static_cast<std::uint32_t>(rt->height())};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(rt->width());
        viewport.height = static_cast<float>(rt->height());
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = {static_cast<std::uint32_t>(rt->width()), static_cast<std::uint32_t>(rt->height())};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, texPipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, context_->texPipelineLayout, 0, 1, &set, 0,
                                nullptr);
        TexPushConstants push;
        push.sourcePremultiplied =
            imageResource->alphaType() == ImageAlphaType::Premultiplied ? 1 : 0;
        vkCmdPushConstants(cmd, context_->texPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(TexPushConstants), &push);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
        vkCmdDraw(cmd, 6, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        submitted = context_->endSingleTimeCommands(cmd);
    }

    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexMemory, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);

    if (!submitted) {
        return false;
    }
    rt->setLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    rt->markReadbackReady();
    context_->readbackImage = rt->image();
    context_->readbackLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    context_->readbackWidth = rt->width();
    context_->readbackHeight = rt->height();
    return true;
#else
    (void)target;
    (void)imageResource;
    return false;
#endif
}

SharedImageResource VulkanRenderDevice::wrapExternalImageResource(ImageResourceHandle handle) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || !handle.isValid()) {
        return nullptr;
    }
    // The handle carries a foreign VkImage (assumed RGBA8, single mip level, and
    // already in SHADER_READ_ONLY layout). We create a non-owning wrapper that
    // owns only its view + sampler and never destroys the borrowed image.
    VkImage image = reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(handle.value));
    VkDevice device = context_->device;

    VkImageView view = VK_NULL_HANDLE;
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kRenderTargetFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        return nullptr;
    }

    VkSampler sampler = VK_NULL_HANDLE;
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        vkDestroyImageView(device, view, nullptr);
        return nullptr;
    }

    return std::make_shared<VulkanTextureResource>(context_.get(), image, VK_NULL_HANDLE, view, sampler,
                                                   /*width=*/0, /*height=*/0, /*mipLevels=*/1,
                                                   /*ownsImage=*/false);
#else
    (void)handle;
    return nullptr;
#endif
}

ImageResourceHandle VulkanRenderDevice::nativeImageHandle(const SharedImageResource &resource) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    auto *tex = dynamic_cast<VulkanTextureResource *>(resource.get());
    if (tex == nullptr || !tex->isValid()) {
        return ImageResourceHandle{};
    }
    return ImageResourceHandle{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(tex->image()))};
#else
    (void)resource;
    return ImageResourceHandle{};
#endif
}

bool VulkanRenderDevice::compositeLayer(const std::unique_ptr<IRenderTarget> &dstTarget,
                                        const std::unique_ptr<IRenderTarget> &layerTarget, float bgR, float bgG,
                                        float bgB, float bgA, float layerAlpha) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || !dstTarget || !layerTarget) {
        return false;
    }
    auto *dst = dynamic_cast<VulkanRenderTarget *>(dstTarget.get());
    auto *layer = dynamic_cast<VulkanRenderTarget *>(layerTarget.get());
    if (dst == nullptr || layer == nullptr || !dst->isValid() || !layer->isValid()) {
        return false;
    }
    VkPipeline layerTexPipe = context_->ensureTexturePipeline(dst->renderPass(), /*SrcOver*/ 0);
    if (layerTexPipe == VK_NULL_HANDLE) {
        return false;
    }
    VkPipeline bgPipeline = context_->ensureSolidPipeline(dst->renderPass(), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                          /*blendMode=Src*/ 1);
    if (bgPipeline == VK_NULL_HANDLE) {
        return false;
    }

    VkDevice device = context_->device;

    // Temporary view + sampler over the already-rendered layer image.
    VkImageView layerView = VK_NULL_HANDLE;
    VkSampler layerSampler = VK_NULL_HANDLE;
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = layer->image();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kRenderTargetFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &layerView) != VK_SUCCESS) {
        return false;
    }
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &layerSampler) != VK_SUCCESS) {
        vkDestroyImageView(device, layerView, nullptr);
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    auto cleanup = [&]() {
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
        vkDestroySampler(device, layerSampler, nullptr);
        vkDestroyImageView(device, layerView, nullptr);
    };
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        cleanup();
        return false;
    }
    VkDescriptorSetAllocateInfo setAlloc{};
    setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAlloc.descriptorPool = pool;
    setAlloc.descriptorSetCount = 1;
    setAlloc.pSetLayouts = &context_->texDescriptorSetLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &setAlloc, &set) != VK_SUCCESS) {
        cleanup();
        return false;
    }
    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescriptor.imageView = layerView;
    imageDescriptor.sampler = layerSampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageDescriptor;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // Background quad (solid) and layer quad (textured, with UVs).
    const std::vector<float> bgVertices = buildSolidVertices(fullTargetQuad(), bgR, bgG, bgB, bgA);
    const std::array<float, 24> layerQuad = {
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,
    };

    VkBuffer bgBuffer = VK_NULL_HANDLE, layerBuffer = VK_NULL_HANDLE;
    VkDeviceMemory bgMemory = VK_NULL_HANDLE, layerMemory = VK_NULL_HANDLE;
    auto cleanupBuffers = [&]() {
        if (bgBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, bgBuffer, nullptr);
        if (bgMemory != VK_NULL_HANDLE) vkFreeMemory(device, bgMemory, nullptr);
        if (layerBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, layerBuffer, nullptr);
        if (layerMemory != VK_NULL_HANDLE) vkFreeMemory(device, layerMemory, nullptr);
    };

    const VkDeviceSize bgBytes = static_cast<VkDeviceSize>(bgVertices.size()) * sizeof(float);
    const VkDeviceSize layerBytes = layerQuad.size() * sizeof(float);
    void *mapped = nullptr;
    if (!context_->createHostVisibleBuffer(bgBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, bgBuffer, bgMemory)
        || vkMapMemory(device, bgMemory, 0, bgBytes, 0, &mapped) != VK_SUCCESS) {
        cleanupBuffers();
        cleanup();
        return false;
    }
    std::memcpy(mapped, bgVertices.data(), static_cast<std::size_t>(bgBytes));
    vkUnmapMemory(device, bgMemory);
    if (!context_->createHostVisibleBuffer(layerBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, layerBuffer, layerMemory)
        || vkMapMemory(device, layerMemory, 0, layerBytes, 0, &mapped) != VK_SUCCESS) {
        cleanupBuffers();
        cleanup();
        return false;
    }
    std::memcpy(mapped, layerQuad.data(), static_cast<std::size_t>(layerBytes));
    vkUnmapMemory(device, layerMemory);

    VkCommandBuffer cmd = context_->beginSingleTimeCommands();
    bool submitted = false;
    if (cmd != VK_NULL_HANDLE) {
        // Make the layer image sampleable inside the render pass.
        VulkanContext::transitionImageLayout(cmd, layer->image(), layer->layout(),
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkClearValue clearValue{};
        clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = dst->renderPass();
        renderPassInfo.framebuffer = dst->framebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {static_cast<std::uint32_t>(dst->width()),
                                            static_cast<std::uint32_t>(dst->height())};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(dst->width());
        viewport.height = static_cast<float>(dst->height());
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = {static_cast<std::uint32_t>(dst->width()), static_cast<std::uint32_t>(dst->height())};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const VkDeviceSize offset = 0;
        // Background (opaque, Src).
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bgPipeline);
        vkCmdBindVertexBuffers(cmd, 0, 1, &bgBuffer, &offset);
        vkCmdDraw(cmd, 6, 1, 0, 0);
        // Layer (textured, SrcOver, layer alpha).
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layerTexPipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, context_->texPipelineLayout, 0, 1, &set, 0,
                                nullptr);
        TexPushConstants push;
        push.layerAlpha = layerAlpha;
        push.sourcePremultiplied = 1;
        vkCmdPushConstants(cmd, context_->texPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(TexPushConstants), &push);
        vkCmdBindVertexBuffers(cmd, 0, 1, &layerBuffer, &offset);
        vkCmdDraw(cmd, 6, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
        submitted = context_->endSingleTimeCommands(cmd);
    }

    layer->setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    cleanupBuffers();
    cleanup();

    if (!submitted) {
        return false;
    }
    dst->setLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    dst->markReadbackReady();
    context_->readbackImage = dst->image();
    context_->readbackLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    context_->readbackWidth = dst->width();
    context_->readbackHeight = dst->height();
    return true;
#else
    (void)dstTarget;
    (void)layerTarget;
    (void)bgR;
    (void)bgG;
    (void)bgB;
    (void)bgA;
    (void)layerAlpha;
    return false;
#endif
}

SharedImageResource VulkanRenderDevice::renderCommandsToImageResource(
    const std::vector<std::unique_ptr<Command>> &commands, const OffscreenRenderRequest &request) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady) {
        return nullptr;
    }
    int w = request.targetWidth > 0 ? request.targetWidth : request.canvasWidth;
    int h = request.targetHeight > 0 ? request.targetHeight : request.canvasHeight;
    if (w <= 0 || h <= 0) {
        return nullptr;
    }

    auto target = createRenderTarget(w, h);
    if (!target || !target->isValid()) {
        return nullptr;
    }
    // Translate + render the command stream, then snapshot the result into an
    // owning sampled texture the caller can composite later (e.g. saveLayer).
    if (!executeCommands(target, commands, request)) {
        return nullptr;
    }
    auto *renderTarget = dynamic_cast<VulkanRenderTarget *>(target.get());
    if (renderTarget == nullptr) {
        return nullptr;
    }
    return copyRenderTargetToSampledTexture(context_.get(), renderTarget);
#else
    (void)commands;
    (void)request;
    return nullptr;
#endif
}

SharedImageResource VulkanRenderDevice::filterImageResource(
    const SharedImageResource &source, int width, int height,
    const wsc::ImageFilter &filter, FilterExecutionStats *executionStats) const
{
    if (executionStats != nullptr) {
        *executionStats = {};
    }
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || width <= 0 || height <= 0
        || !filter.isValid() || filter.type() != wsc::ImageFilter::Type::Blur) {
        return {};
    }
    auto *input = dynamic_cast<VulkanTextureResource *>(source.get());
    if (input == nullptr || !input->isValid()
        || input->context() != context_.get()
        || input->width() != width || input->height() != height) {
        return {};
    }

    const wsc::render::GaussianBlurDownsample downsample =
        wsc::render::chooseGaussianBlurDownsampleFactors(
            width, height, filter.radiusX(), filter.radiusY());
    const int blurWidth = (width + downsample.x - 1) / downsample.x;
    const int blurHeight = (height + downsample.y - 1) / downsample.y;
    auto targetA = createRenderTarget(blurWidth, blurHeight);
    auto targetB = createRenderTarget(blurWidth, blurHeight);
    auto *passA = dynamic_cast<VulkanRenderTarget *>(targetA.get());
    auto *passB = dynamic_cast<VulkanRenderTarget *>(targetB.get());
    if (passA == nullptr || passB == nullptr
        || !passA->isValid() || !passB->isValid()) {
        return {};
    }

    VkSampler sampler = context_->getSampler(
        VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    if (sampler == VK_NULL_HANDLE) {
        return {};
    }
    const bool decal = filter.tileMode() == wsc::ImageFilter::TileMode::Decal;
    const bool sourcePremultiplied =
        source->alphaType() == ImageAlphaType::Premultiplied;
    const auto kernelX = wsc::render::computeGaussianKernel(
        filter.radiusX() / static_cast<float>(downsample.x));
    const auto kernelY = wsc::render::computeGaussianKernel(
        filter.radiusY() / static_cast<float>(downsample.y));

    VulkanRenderTarget *restore = nullptr;
    std::unique_ptr<IRenderTarget> restoreTarget;
    if (downsample.active()) {
        restoreTarget = createRenderTarget(width, height);
        restore = dynamic_cast<VulkanRenderTarget *>(restoreTarget.get());
        if (restore == nullptr || !restore->isValid()) {
            return {};
        }
    }

    auto result = createEmptySampledTexture(context_.get(), width, height);
    if (!result) {
        return {};
    }

    std::array<FilterPassRecording, 3> recordings{};
    auto destroyRecordings = [&]() {
        for (FilterPassRecording &recording : recordings) {
            recording.destroy(context_->device);
        }
    };
    if (!prepareFilterPass(
            context_.get(), passA, input->view(), sampler, kernelX,
            1.0f / static_cast<float>(blurWidth), 0.0f, decal,
            1.0f, 1.0f, 1.0f, 0.0f, sourcePremultiplied, false,
            downsample.active() && !sourcePremultiplied, recordings[0])
        || !prepareFilterPass(
            context_.get(), passB, passA->view(), sampler, kernelY,
            0.0f, 1.0f / static_cast<float>(blurHeight), decal,
            !downsample.active() ? filter.saturation() : 1.0f,
            !downsample.active() ? filter.brightness() : 1.0f,
            !downsample.active() ? filter.contrast() : 1.0f,
            !downsample.active() ? filter.grain() : 0.0f,
            true, !downsample.active(), false, recordings[1])) {
        destroyRecordings();
        return {};
    }

    VulkanRenderTarget *finalTarget = passB;
    if (downsample.active()) {
        const auto passThrough = wsc::render::computeGaussianKernel(0.0f);
        if (!prepareFilterPass(
                context_.get(), restore, passB->view(), sampler, passThrough,
                0.0f, 0.0f, decal,
                filter.saturation(), filter.brightness(), filter.contrast(),
                filter.grain(), true, true, false, recordings[2])) {
            destroyRecordings();
            return {};
        }
        finalTarget = restore;
    }

    VkCommandBuffer commandBuffer = context_->beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) {
        destroyRecordings();
        return {};
    }

    recordFilterPass(context_.get(), commandBuffer, passA, recordings[0]);
    VulkanContext::transitionImageLayout(
        commandBuffer, passA->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    recordFilterPass(context_.get(), commandBuffer, passB, recordings[1]);
    if (restore != nullptr) {
        VulkanContext::transitionImageLayout(
            commandBuffer, passB->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        recordFilterPass(context_.get(), commandBuffer, restore, recordings[2]);
    }

    // The final render pass already selected TRANSFER_SRC_OPTIMAL. Keep the
    // layout but add the required color-write to transfer-read dependency.
    VulkanContext::transitionImageLayout(
        commandBuffer, finalTarget->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VulkanContext::transitionImageLayout(
        commandBuffer, result->image(), VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.layerCount = 1;
    copy.extent = {static_cast<std::uint32_t>(width),
                   static_cast<std::uint32_t>(height), 1};
    vkCmdCopyImage(
        commandBuffer,
        finalTarget->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        result->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copy);
    VulkanContext::transitionImageLayout(
        commandBuffer, result->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const bool submitted = context_->endSingleTimeCommands(commandBuffer);
    destroyRecordings();
    if (!submitted) {
        return {};
    }
    passA->markSynchronized();
    passB->markSynchronized();
    if (restore != nullptr) {
        restore->markSynchronized();
    }
    if (!result->isValid()) {
        return {};
    }
    passA->setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    passB->setLayout(
        restore != nullptr ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                           : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    if (restore != nullptr) {
        restore->setLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }
    if (executionStats != nullptr) {
        executionStats->passCount = downsample.active() ? 3u : 2u;
        executionStats->pixelPassCount =
            static_cast<std::size_t>(blurWidth)
                * static_cast<std::size_t>(blurHeight) * 2u
            + (downsample.active()
                ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
                : 0u);
        executionStats->downsampled = downsample.active();
    }
    return result;
#else
    (void)source;
    (void)width;
    (void)height;
    (void)filter;
    return {};
#endif
}

bool VulkanRenderDevice::executeDrawList(const std::unique_ptr<IRenderTarget> &target,
                                         const wsc::DrawList &drawList) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || !target || drawList.empty()) {
        return false;
    }
    auto *rt = dynamic_cast<VulkanRenderTarget *>(target.get());
    if (rt == nullptr || !rt->isValid()) {
        return false;
    }

    VkDevice device = context_->device;

    std::uint32_t texturedCount = 0;
    std::uint32_t clipCount = 0;
    std::uint32_t gradientCount = 0;
    for (const wsc::DrawPrimitive &prim : drawList) {
        if (prim.kind == wsc::DrawPrimitiveKind::TexturedQuad) {
            ++texturedCount;
        } else if (prim.kind == wsc::DrawPrimitiveKind::ClipFill) {
            ++clipCount;
        } else if (prim.kind == wsc::DrawPrimitiveKind::GradientFill) {
            ++gradientCount;
        }
    }

    if (texturedCount > 0 && context_->ensureTexturePipeline(rt->renderPass(), 0) == VK_NULL_HANDLE) {
        return false;
    }
    if (clipCount > 0 && context_->ensureClipPipeline(rt->renderPass(), 0) == VK_NULL_HANDLE) {
        return false;
    }
    if (gradientCount > 0 && context_->ensureGradientPipeline(rt->renderPass(), 0) == VK_NULL_HANDLE) {
        return false;
    }

    VkDescriptorPool pool = VK_NULL_HANDLE;
    const std::uint32_t samplerSets = texturedCount + clipCount;
    const std::uint32_t totalSets = samplerSets + gradientCount;
    if (totalSets > 0) {
        std::vector<VkDescriptorPoolSize> poolSizes;
        if (samplerSets > 0) {
            poolSizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, samplerSets});
        }
        if (gradientCount > 0) {
            poolSizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, gradientCount});
        }
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = totalSets;
        poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
            return false;
        }
    }

    struct RecordedDraw
    {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        std::uint32_t vertexCount = 0;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkPipelineLayout descriptorLayout = VK_NULL_HANDLE;
        bool pushLayerAlpha = false;
        TexPushConstants push;
        VkBuffer uboBuffer = VK_NULL_HANDLE;
        VkDeviceMemory uboMemory = VK_NULL_HANDLE;
    };
    std::vector<RecordedDraw> draws;
    draws.reserve(drawList.size());

    bool ok = true;
    auto cleanup = [&]() {
        for (RecordedDraw &d : draws) {
            if (d.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, d.buffer, nullptr);
            if (d.memory != VK_NULL_HANDLE) vkFreeMemory(device, d.memory, nullptr);
            if (d.uboBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, d.uboBuffer, nullptr);
            if (d.uboMemory != VK_NULL_HANDLE) vkFreeMemory(device, d.uboMemory, nullptr);
        }
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
    };

    // Bind a texture resource to a freshly allocated descriptor set of the given
    // layout. Returns VK_NULL_HANDLE on failure.
    auto allocSampledSet = [&](VulkanTextureResource *tex, VkDescriptorSetLayout layout,
                               VkSampler sampler) -> VkDescriptorSet {
        VkDescriptorSetAllocateInfo setAlloc{};
        setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setAlloc.descriptorPool = pool;
        setAlloc.descriptorSetCount = 1;
        setAlloc.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device, &setAlloc, &set) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        VkDescriptorImageInfo imageDescriptor{};
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageDescriptor.imageView = tex->view();
        imageDescriptor.sampler = sampler;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        return set;
    };

    for (const wsc::DrawPrimitive &prim : drawList) {
        RecordedDraw draw;
        std::vector<float> vertices;
        if (prim.kind == wsc::DrawPrimitiveKind::SolidTriangles) {
            const std::size_t vertexCount = prim.positions.size() / 2;
            if (vertexCount < 3 || (vertexCount % 3) != 0) {
                ok = false;
                break;
            }
            draw.pipeline = context_->ensureSolidPipeline(rt->renderPass(), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                          prim.blendMode);
            const bool perVertexColor = prim.colors.size() == vertexCount * 4;
            const bool perVertexCoverage = prim.coverage.size() == vertexCount;
            if (perVertexColor || perVertexCoverage) {
                vertices.reserve(vertexCount * 7);
                for (std::size_t v = 0; v < vertexCount; ++v) {
                    vertices.push_back(prim.positions[v * 2 + 0]);
                    vertices.push_back(prim.positions[v * 2 + 1]);
                    if (perVertexColor) {
                        vertices.push_back(prim.colors[v * 4 + 0]);
                        vertices.push_back(prim.colors[v * 4 + 1]);
                        vertices.push_back(prim.colors[v * 4 + 2]);
                        vertices.push_back(prim.colors[v * 4 + 3]);
                    } else {
                        vertices.push_back(prim.color[0]);
                        vertices.push_back(prim.color[1]);
                        vertices.push_back(prim.color[2]);
                        vertices.push_back(prim.color[3]);
                    }
                    vertices.push_back(perVertexCoverage ? prim.coverage[v] : 1.0f);
                }
            } else {
                vertices = buildSolidVertices(prim.positions, prim.color[0], prim.color[1], prim.color[2],
                                              prim.color[3]);
            }
            draw.vertexCount = static_cast<std::uint32_t>(vertexCount);
        } else if (prim.kind == wsc::DrawPrimitiveKind::TexturedQuad) {
            auto *tex = dynamic_cast<VulkanTextureResource *>(prim.texture.get());
            if (tex == nullptr || !tex->isValid()) {
                ok = false;
                break;
            }
            draw.pipeline = context_->ensureTexturePipeline(rt->renderPass(), prim.blendMode);
            if (draw.pipeline == VK_NULL_HANDLE) {
                ok = false;
                break;
            }
            draw.descriptorLayout = context_->texPipelineLayout;
            draw.pushLayerAlpha = true;
            draw.push.layerAlpha = prim.layerAlpha;
            draw.push.sourcePremultiplied =
                prim.texture->alphaType() == ImageAlphaType::Premultiplied ? 1 : 0;
            draw.push.tint[0] = prim.tint[0];
            draw.push.tint[1] = prim.tint[1];
            draw.push.tint[2] = prim.tint[2];
            draw.push.tint[3] = prim.tint[3];
            draw.push.useColorMatrix = prim.hasColorMatrix ? 1 : 0;
            if (prim.hasColorMatrix) {
                std::memcpy(draw.push.colorMatrix, prim.colorMatrix, sizeof(draw.push.colorMatrix));
                std::memcpy(draw.push.colorOffset, prim.colorMatrixOffset, sizeof(draw.push.colorOffset));
            }
            VkSampler sampler = tex->sampler();
            if (prim.useCustomSampler) {
                const VkFilter filter = (prim.sampling == 1) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
                VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                switch (prim.tileMode) {
                case 1:
                    addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    break;
                case 2:
                    addressMode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                    break;
                case 3:
                    addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
                    break;
                default:
                    addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    break;
                }
                sampler = context_->getSampler(filter, addressMode,
                                               /*mipmap=*/prim.sampling == 2 && tex->mipLevels() > 1);
            }
            if (sampler == VK_NULL_HANDLE) {
                ok = false;
                break;
            }
            draw.descriptorSet = allocSampledSet(tex, context_->texDescriptorSetLayout, sampler);
            if (draw.descriptorSet == VK_NULL_HANDLE) {
                ok = false;
                break;
            }
            const std::size_t explicitVerts = prim.positions.size() / 2;
            if (explicitVerts >= 3 && (explicitVerts % 3) == 0 && prim.uvs.size() == prim.positions.size()) {
                vertices.reserve(explicitVerts * 4);
                for (std::size_t v = 0; v < explicitVerts; ++v) {
                    vertices.push_back(prim.positions[v * 2 + 0]);
                    vertices.push_back(prim.positions[v * 2 + 1]);
                    vertices.push_back(prim.uvs[v * 2 + 0]);
                    vertices.push_back(prim.uvs[v * 2 + 1]);
                }
                draw.vertexCount = static_cast<std::uint32_t>(explicitVerts);
            } else {
                vertices = {
                    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,
                };
                draw.vertexCount = 6;
            }
        } else if (prim.kind == wsc::DrawPrimitiveKind::ClipFill) {
            auto *mask = dynamic_cast<VulkanTextureResource *>(prim.texture.get());
            if (mask == nullptr || !mask->isValid()) {
                ok = false;
                break;
            }
            draw.pipeline = context_->ensureClipPipeline(rt->renderPass(), prim.blendMode);
            if (draw.pipeline == VK_NULL_HANDLE) {
                ok = false;
                break;
            }
            draw.descriptorLayout = context_->clipPipelineLayout;
            draw.descriptorSet = allocSampledSet(mask, context_->clipDescriptorSetLayout, mask->sampler());
            if (draw.descriptorSet == VK_NULL_HANDLE) {
                ok = false;
                break;
            }
            const float r = prim.color[0], g = prim.color[1], b = prim.color[2], a = prim.color[3];
            if (!prim.positions.empty() && prim.uvs.size() == prim.positions.size()) {
                // Arbitrary clipped geometry: the clip shader modulates each
                // fragment's alpha by the mask sampled at its screen-space UV.
                const std::size_t vc = prim.positions.size() / 2;
                const bool perVertexColor = prim.colors.size() == vc * 4;
                vertices.reserve(vc * 8);
                for (std::size_t v = 0; v < vc; ++v) {
                    vertices.push_back(prim.positions[v * 2 + 0]);
                    vertices.push_back(prim.positions[v * 2 + 1]);
                    if (perVertexColor) {
                        vertices.push_back(prim.colors[v * 4 + 0]);
                        vertices.push_back(prim.colors[v * 4 + 1]);
                        vertices.push_back(prim.colors[v * 4 + 2]);
                        vertices.push_back(prim.colors[v * 4 + 3]);
                    } else {
                        vertices.push_back(r);
                        vertices.push_back(g);
                        vertices.push_back(b);
                        vertices.push_back(a);
                    }
                    vertices.push_back(prim.uvs[v * 2 + 0]);
                    vertices.push_back(prim.uvs[v * 2 + 1]);
                }
                draw.vertexCount = static_cast<std::uint32_t>(vc);
            } else {
                // Full-target quad (fills the whole target, clipped to the mask).
                vertices = {
                    -1.0f, -1.0f, r, g, b, a, 0.0f, 0.0f, 1.0f, -1.0f, r, g, b, a, 1.0f, 0.0f,
                    1.0f,  1.0f,  r, g, b, a, 1.0f, 1.0f, -1.0f, -1.0f, r, g, b, a, 0.0f, 0.0f,
                    1.0f,  1.0f,  r, g, b, a, 1.0f, 1.0f, -1.0f, 1.0f,  r, g, b, a, 0.0f, 1.0f,
                };
                draw.vertexCount = 6;
            }
        } else { // GradientFill
            const std::size_t vertexCount = prim.positions.size() / 2;
            if (vertexCount < 3 || (vertexCount % 3) != 0 || prim.localPositions.size() != prim.positions.size()) {
                ok = false;
                break;
            }
            if (!context_->createHostVisibleBuffer(sizeof(GradientUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   draw.uboBuffer, draw.uboMemory)) {
                ok = false;
                break;
            }
            GradientUBO ubo;
            ubo.linear[0] = prim.linearStart[0];
            ubo.linear[1] = prim.linearStart[1];
            ubo.linear[2] = prim.linearEnd[0];
            ubo.linear[3] = prim.linearEnd[1];
            ubo.radial[0] = prim.radialCenter[0];
            ubo.radial[1] = prim.radialCenter[1];
            ubo.radial[2] = prim.radialRadius;
            ubo.radial[3] = static_cast<float>(prim.gradientType);
            ubo.modeCount[0] = prim.gradientTileMode;
            ubo.modeCount[1] = prim.gradientStopCount;
            for (int i = 0; i < 8; ++i) {
                const bool has = i < prim.gradientStopCount;
                const float p = has ? prim.gradientStopPositions[i] : 0.0f;
                if (i < 4) {
                    ubo.stopPos0[i] = p;
                } else {
                    ubo.stopPos1[i - 4] = p;
                }
                for (int c = 0; c < 4; ++c) {
                    ubo.stopColors[i][c] = has ? prim.gradientStopColors[i * 4 + c] : 0.0f;
                }
            }
            void *uboMapped = nullptr;
            if (vkMapMemory(device, draw.uboMemory, 0, sizeof(GradientUBO), 0, &uboMapped) != VK_SUCCESS) {
                ok = false;
                break;
            }
            std::memcpy(uboMapped, &ubo, sizeof(GradientUBO));
            vkUnmapMemory(device, draw.uboMemory);

            VkDescriptorSetAllocateInfo setAlloc{};
            setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            setAlloc.descriptorPool = pool;
            setAlloc.descriptorSetCount = 1;
            setAlloc.pSetLayouts = &context_->gradientDescriptorSetLayout;
            if (vkAllocateDescriptorSets(device, &setAlloc, &draw.descriptorSet) != VK_SUCCESS) {
                ok = false;
                break;
            }
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = draw.uboBuffer;
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(GradientUBO);
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = draw.descriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

            draw.pipeline = context_->ensureGradientPipeline(rt->renderPass(), prim.blendMode);
            if (draw.pipeline == VK_NULL_HANDLE) {
                ok = false;
                break;
            }
            draw.descriptorLayout = context_->gradientPipelineLayout;
            vertices.reserve(vertexCount * 4);
            for (std::size_t v = 0; v < vertexCount; ++v) {
                vertices.push_back(prim.positions[v * 2 + 0]);
                vertices.push_back(prim.positions[v * 2 + 1]);
                vertices.push_back(prim.localPositions[v * 2 + 0]);
                vertices.push_back(prim.localPositions[v * 2 + 1]);
            }
            draw.vertexCount = static_cast<std::uint32_t>(vertexCount);
        }

        if (draw.pipeline == VK_NULL_HANDLE) {
            ok = false;
            break;
        }
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(vertices.size()) * sizeof(float);
        if (!context_->createHostVisibleBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, draw.buffer, draw.memory)) {
            ok = false;
            break;
        }
        void *mapped = nullptr;
        if (vkMapMemory(device, draw.memory, 0, bytes, 0, &mapped) != VK_SUCCESS) {
            ok = false;
            draws.push_back(draw);
            break;
        }
        std::memcpy(mapped, vertices.data(), static_cast<std::size_t>(bytes));
        vkUnmapMemory(device, draw.memory);
        draws.push_back(draw);
    }

    if (!ok) {
        cleanup();
        return false;
    }

    VkCommandBuffer cmd = context_->beginSingleTimeCommands();
    bool submitted = false;
    if (cmd != VK_NULL_HANDLE) {
        VkClearValue clearValue{};
        clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = rt->renderPass();
        renderPassInfo.framebuffer = rt->framebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {static_cast<std::uint32_t>(rt->width()),
                                            static_cast<std::uint32_t>(rt->height())};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(rt->width());
        viewport.height = static_cast<float>(rt->height());
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = {static_cast<std::uint32_t>(rt->width()), static_cast<std::uint32_t>(rt->height())};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const VkDeviceSize offset = 0;
        for (const RecordedDraw &d : draws) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.pipeline);
            if (d.descriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.descriptorLayout, 0, 1,
                                        &d.descriptorSet, 0, nullptr);
                if (d.pushLayerAlpha) {
                    vkCmdPushConstants(cmd, d.descriptorLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(TexPushConstants), &d.push);
                }
            }
            vkCmdBindVertexBuffers(cmd, 0, 1, &d.buffer, &offset);
            vkCmdDraw(cmd, d.vertexCount, 1, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
        submitted = context_->endSingleTimeCommands(cmd);
    }

    cleanup();

    if (!submitted) {
        return false;
    }
    rt->setLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    rt->markReadbackReady();
    context_->readbackImage = rt->image();
    context_->readbackLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    context_->readbackWidth = rt->width();
    context_->readbackHeight = rt->height();
    return true;
#else
    (void)target;
    (void)drawList;
    return false;
#endif
}

bool VulkanRenderDevice::executeCommands(const std::unique_ptr<IRenderTarget> &target,
                                         const std::vector<std::unique_ptr<Command>> &commands,
                                         const OffscreenRenderRequest &request) const
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady) {
        return false;
    }
    // Redirect into a host-owned external target when wrap-external is active.
    auto *rt = context_->externalRenderTarget
                   ? dynamic_cast<VulkanRenderTarget *>(context_->externalRenderTarget.get())
                   : (target ? dynamic_cast<VulkanRenderTarget *>(target.get()) : nullptr);
    if (rt == nullptr || !rt->isValid()) {
        return false;
    }
    const float w = request.canvasWidth > 0 ? static_cast<float>(request.canvasWidth)
                                            : static_cast<float>(rt->width());
    const float h = request.canvasHeight > 0 ? static_cast<float>(request.canvasHeight)
                                            : static_cast<float>(rt->height());
    if (w <= 0.0f || h <= 0.0f) {
        return false;
    }

    // Map the Canvas blend model to the Vulkan solid blend-mode indices
    // (see blendStateFor, which mirrors RenderContext::applyBlendMode exactly).
    auto mapBlend = [](DrawBlendMode mode) -> int {
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
            return 0; // SrcOver
        }
    };

    // Project a canvas-space point through a transform to Vulkan NDC using the
    // same cropped viewport semantics as OpenGLRenderTarget::activate().
    auto toNdc = [&](const glm::mat4 &tf, float x, float y, float &ox, float &oy) {
        const glm::vec4 p = tf * glm::vec4(x, y, 0.0f, 1.0f);
        const float targetX = p.x + static_cast<float>(request.viewportX);
        const float targetY =
            p.y + static_cast<float>(rt->height() - request.viewportY) - h;
        ox = targetX / static_cast<float>(rt->width()) * 2.0f - 1.0f;
        oy = targetY / static_cast<float>(rt->height()) * 2.0f - 1.0f;
    };
    auto toFullCanvasNdc = [&](const glm::mat4 &tf, float x, float y,
                               float &ox, float &oy) {
        const glm::vec4 p = tf * glm::vec4(x, y, 0.0f, 1.0f);
        ox = p.x / w * 2.0f - 1.0f;
        oy = p.y / h * 2.0f - 1.0f;
    };
    auto targetNdcToCanvasUv = [&](float nx, float ny) {
        const float targetX =
            (nx + 1.0f) * 0.5f * static_cast<float>(rt->width());
        const float targetY =
            (ny + 1.0f) * 0.5f * static_cast<float>(rt->height());
        const float canvasX = targetX - static_cast<float>(request.viewportX);
        const float canvasY = targetY
            - static_cast<float>(rt->height() - request.viewportY) + h;
        return glm::vec2(canvasX / w, canvasY / h);
    };

    // Emit two triangles (a quad ABCD) into an NDC position list.
    auto emitQuad = [&](std::vector<float> &out, const glm::mat4 &tf, float ax, float ay, float bx, float by,
                        float cx, float cy, float dx, float dy) {
        float n[8];
        toNdc(tf, ax, ay, n[0], n[1]);
        toNdc(tf, bx, by, n[2], n[3]);
        toNdc(tf, cx, cy, n[4], n[5]);
        toNdc(tf, dx, dy, n[6], n[7]);
        const float tris[12] = {n[0], n[1], n[2], n[3], n[4], n[5], n[0], n[1], n[4], n[5], n[6], n[7]};
        out.insert(out.end(), tris, tris + 12);
    };

    // Build a clip coverage mask texture (red channel = coverage) for the given
    // clip state. Each clip path is rasterized (white, with its analytic-AA
    // coverage) into an offscreen target over black -- SrcOver leaves red ==
    // coverage -- read back, and multiplied together (nested clips intersect).
    // Returns null on failure. Mirrors the GL clip mask sampled as `.r`.
    // Backend-neutral primitives accumulated for this command stream. Declared
    // before the clip helpers so `emitClippedLayer` can append its composite.
    wsc::DrawList list;

    const int maskW = static_cast<int>(w);
    const int maskH = static_cast<int>(h);
    const std::size_t maskPixelCount = static_cast<std::size_t>(maskW) * static_cast<std::size_t>(maskH);
    auto buildClipCoverage = [&](const ClipMaskState &clip, std::vector<float> &outCoverage) -> bool {
        if (maskW <= 0 || maskH <= 0 || clip.resources.empty()) {
            return false;
        }
        outCoverage.assign(maskPixelCount, 1.0f);
        for (const SharedClipMaskResource &res : clip.resources) {
            auto *cmr = dynamic_cast<VulkanClipMaskResource *>(res.get());
            if (cmr == nullptr || !cmr->isValid()) {
                return false;
            }
            const std::vector<float> &pts = cmr->points();
            const std::vector<float> &cov = cmr->coverage();
            const glm::mat4 &tf = cmr->transform();
            const std::size_t vc = pts.size() / 2;
            if (vc < 3 || (vc % 3) != 0) {
                return false;
            }
            std::unique_ptr<IRenderTarget> covTarget = createRenderTarget(maskW, maskH);
            if (!covTarget || !covTarget->isValid()) {
                return false;
            }
            wsc::DrawList ml;
            wsc::DrawPrimitive mp;
            mp.kind = wsc::DrawPrimitiveKind::SolidTriangles;
            mp.blendMode = 0; // SrcOver over cleared black -> red == coverage
            mp.positions.reserve(vc * 2);
            for (std::size_t i = 0; i < vc; ++i) {
                float nx = 0.0f, ny = 0.0f;
                toFullCanvasNdc(tf, pts[i * 2 + 0], pts[i * 2 + 1], nx, ny);
                mp.positions.push_back(nx);
                mp.positions.push_back(ny);
            }
            mp.color[0] = 1.0f;
            mp.color[1] = 1.0f;
            mp.color[2] = 1.0f;
            mp.color[3] = 1.0f;
            if (cov.size() == vc) {
                mp.coverage = cov;
            }
            ml.push_back(std::move(mp));
            if (!executeDrawList(covTarget, ml)) {
                return false;
            }
            std::vector<unsigned char> px;
            if (!readPixelsRGBA(maskW, maskH, px)) {
                return false;
            }
            for (std::size_t i = 0; i < maskPixelCount; ++i) {
                outCoverage[i] *= static_cast<float>(px[i * 4 + 0]) / 255.0f;
            }
        }
        return true;
    };

    // Build a clip coverage mask texture (red channel = coverage) for the given
    // clip state. Mirrors the GL clip mask sampled as `.r`. Returns null on failure.
    auto buildClipMaskTexture = [&](const ClipMaskState &clip) -> SharedImageResource {
        std::vector<float> combined;
        if (!buildClipCoverage(clip, combined)) {
            return nullptr;
        }
        std::vector<unsigned char> maskPixels(maskPixelCount * 4, 0);
        for (std::size_t i = 0; i < maskPixelCount; ++i) {
            const unsigned char c = static_cast<unsigned char>(combined[i] * 255.0f + 0.5f);
            maskPixels[i * 4 + 0] = c; // clip shader samples .r
            maskPixels[i * 4 + 3] = 255;
        }
        return createSampledTexture(context_.get(), maskW, maskH, maskPixels.data(), /*nearest=*/false);
    };

    // Convert an already-built solid primitive (positions + color, and optional
    // per-vertex colors) into a ClipFill against the given clip state: builds the
    // clip mask and sets per-vertex screen-space UVs so the clip pipeline
    // modulates the fill by the mask. Returns false if the mask could not be
    // built (caller should drop the command).
    auto emitClippedSolid = [&](wsc::DrawPrimitive &prim, const ClipMaskState &clip) -> bool {
        SharedImageResource clipTex = buildClipMaskTexture(clip);
        if (!clipTex) {
            return false;
        }
        const std::size_t vc = prim.positions.size() / 2;
        prim.kind = wsc::DrawPrimitiveKind::ClipFill;
        prim.texture = clipTex;
        prim.uvs.clear();
        prim.uvs.reserve(vc * 2);
        for (std::size_t i = 0; i < vc; ++i) {
            const glm::vec2 uv = targetNdcToCanvasUv(
                prim.positions[i * 2 + 0], prim.positions[i * 2 + 1]);
            prim.uvs.push_back(uv.x);
            prim.uvs.push_back(uv.y);
        }
        return true;
    };

    // Clip an arbitrary primitive (gradient or textured image) that the clip
    // pipeline cannot modulate directly: render it in isolation into an offscreen
    // layer, un-premultiply, multiply its alpha by the clip coverage, and
    // composite the result as a full-canvas textured quad in stream order. (The
    // isolated layer is rendered SrcOver over transparent, so its RGB is
    // premultiplied; un-premultiplying restores straight alpha for the composite.)
    auto emitClippedLayer = [&](wsc::DrawPrimitive srcPrim, const ClipMaskState &clip, int compositeBlend) -> bool {
        std::vector<float> cov;
        if (!buildClipCoverage(clip, cov)) {
            return false;
        }
        std::unique_ptr<IRenderTarget> layer = createRenderTarget(maskW, maskH);
        if (!layer || !layer->isValid()) {
            return false;
        }
        wsc::DrawList ll;
        // Render the primitive in isolation with SrcOver over the cleared
        // (transparent) layer so its RGB is a well-defined premultiplied capture;
        // the draw's actual blend mode is applied later at composite time.
        srcPrim.blendMode = 0;
        for (std::size_t i = 0; i + 1 < srcPrim.positions.size(); i += 2) {
            const glm::vec2 uv =
                targetNdcToCanvasUv(srcPrim.positions[i], srcPrim.positions[i + 1]);
            srcPrim.positions[i] = uv.x * 2.0f - 1.0f;
            srcPrim.positions[i + 1] = uv.y * 2.0f - 1.0f;
        }
        ll.push_back(std::move(srcPrim));
        if (!executeDrawList(layer, ll)) {
            return false;
        }
        std::vector<unsigned char> lpx;
        if (!readPixelsRGBA(maskW, maskH, lpx)) {
            return false;
        }
        std::vector<unsigned char> out(maskPixelCount * 4);
        for (std::size_t i = 0; i < maskPixelCount; ++i) {
            const float a = static_cast<float>(lpx[i * 4 + 3]) / 255.0f;
            for (int c = 0; c < 3; ++c) {
                const float pr = static_cast<float>(lpx[i * 4 + c]) / 255.0f;
                const float straight = a > 0.0001f ? pr / a : 0.0f;
                const float sc = straight < 0.0f ? 0.0f : (straight > 1.0f ? 1.0f : straight);
                out[i * 4 + c] = static_cast<unsigned char>(sc * 255.0f + 0.5f);
            }
            const float ca = a * cov[i];
            out[i * 4 + 3] = static_cast<unsigned char>(ca * 255.0f + 0.5f);
        }
        SharedImageResource layerTex =
            createSampledTexture(context_.get(), maskW, maskH, out.data(), /*nearest=*/false);
        if (!layerTex) {
            return false;
        }
        float lnx[4], lny[4];
        toNdc(glm::mat4(1.0f), 0.0f, 0.0f, lnx[0], lny[0]);
        toNdc(glm::mat4(1.0f), w, 0.0f, lnx[1], lny[1]);
        toNdc(glm::mat4(1.0f), w, h, lnx[2], lny[2]);
        toNdc(glm::mat4(1.0f), 0.0f, h, lnx[3], lny[3]);
        const float lu[4] = {0.0f, 1.0f, 1.0f, 0.0f};
        const float lv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        const int lidx[6] = {0, 1, 2, 0, 2, 3};
        wsc::DrawPrimitive q;
        q.kind = wsc::DrawPrimitiveKind::TexturedQuad;
        q.blendMode = compositeBlend;
        q.texture = layerTex;
        q.layerAlpha = 1.0f;
        q.tint[0] = 1.0f;
        q.tint[1] = 1.0f;
        q.tint[2] = 1.0f;
        q.tint[3] = 1.0f;
        q.sampling = static_cast<int>(DrawImageSampling::Linear);
        q.tileMode = static_cast<int>(DrawImageTileMode::Clamp);
        q.useCustomSampler = true;
        q.positions.reserve(12);
        q.uvs.reserve(12);
        for (int k : lidx) {
            q.positions.push_back(lnx[k]);
            q.positions.push_back(lny[k]);
            q.uvs.push_back(lu[k]);
            q.uvs.push_back(lv[k]);
        }
        list.push_back(std::move(q));
        return true;
    };

    // Translate the real Command stream into backend-neutral primitives.
    for (const std::unique_ptr<Command> &cmd : commands) {
        if (!cmd) {
            continue;
        }

        if (cmd->type() == Command::Type::Path) {
            const auto *pathCmd = static_cast<const DrawPathCommand *>(cmd.get());
            const DrawPathData &d = pathCmd->data();
            const bool clipped = d.clipMask.hasPaths();
            const std::size_t vertexCount = d.getPointCount();
            if (vertexCount < 3 || (vertexCount % 3) != 0) {
                continue;
            }
            wsc::DrawPrimitive prim;
            prim.blendMode = mapBlend(d.blendMode);
            prim.positions.reserve(vertexCount * 2);
            for (std::size_t i = 0; i < vertexCount; ++i) {
                float nx = 0.0f, ny = 0.0f;
                toNdc(d.transform, d.points[i * 2 + 0], d.points[i * 2 + 1], nx, ny);
                prim.positions.push_back(nx);
                prim.positions.push_back(ny);
            }
            if (clipped && !d.hasShaderGradient()) {
                // Clipped solid fills: modulate the fill by a clip coverage mask
                // sampled at each fragment's screen position (mirrors the GL
                // clip-mask fragment path).
                prim.color[0] = d.color[0];
                prim.color[1] = d.color[1];
                prim.color[2] = d.color[2];
                prim.color[3] = d.color[3];
                if (d.hasCoverage()) {
                    // Bake the fill's own analytic-AA coverage into per-vertex alpha
                    // (the clip pipeline has no coverage attribute); the clip shader
                    // then multiplies by the mask coverage as well.
                    prim.colors.resize(vertexCount * 4);
                    for (std::size_t i = 0; i < vertexCount; ++i) {
                        prim.colors[i * 4 + 0] = d.color[0];
                        prim.colors[i * 4 + 1] = d.color[1];
                        prim.colors[i * 4 + 2] = d.color[2];
                        prim.colors[i * 4 + 3] = d.color[3] * d.coverage[i];
                    }
                }
                if (!emitClippedSolid(prim, d.clipMask)) {
                    continue;
                }
                list.push_back(std::move(prim));
                continue;
            }
            if (d.hasShaderGradient()) {
                prim.kind = wsc::DrawPrimitiveKind::GradientFill;
                // Local (canvas-space) positions matching the transformed geometry.
                prim.localPositions.reserve(vertexCount * 2);
                for (std::size_t i = 0; i < vertexCount; ++i) {
                    const glm::vec4 p =
                        d.transform * glm::vec4(d.points[i * 2 + 0], d.points[i * 2 + 1], 0.0f, 1.0f);
                    prim.localPositions.push_back(p.x);
                    prim.localPositions.push_back(p.y);
                }
                prim.gradientType = static_cast<int>(d.gradientType);
                prim.gradientTileMode = static_cast<int>(d.gradientTileMode);
                prim.linearStart[0] = d.gradientStart[0];
                prim.linearStart[1] = d.gradientStart[1];
                prim.linearEnd[0] = d.gradientEnd[0];
                prim.linearEnd[1] = d.gradientEnd[1];
                prim.radialCenter[0] = d.radialCenter[0];
                prim.radialCenter[1] = d.radialCenter[1];
                prim.radialRadius = d.radialRadius;
                prim.gradientStopCount = d.gradientStopCount;
                for (int i = 0; i < d.gradientStopCount && i < 8; ++i) {
                    prim.gradientStopPositions[i] = d.gradientStopPositions[i];
                    for (int c = 0; c < 4; ++c) {
                        prim.gradientStopColors[i * 4 + c] = d.gradientStopColors[i * 4 + c];
                    }
                }
            } else {
                prim.kind = wsc::DrawPrimitiveKind::SolidTriangles;
                if (d.hasVertexColors()) {
                    prim.colors = d.colors;
                }
                if (d.hasCoverage()) {
                    prim.coverage = d.coverage; // analytic-AA edge feathering
                }
                prim.color[0] = d.color[0];
                prim.color[1] = d.color[1];
                prim.color[2] = d.color[2];
                prim.color[3] = d.color[3];
            }
            if (clipped) {
                // Clipped gradient fill (solid clip is handled above): render the
                // gradient in isolation and multiply its alpha by the clip mask.
                if (!emitClippedLayer(std::move(prim), d.clipMask, mapBlend(d.blendMode))) {
                    continue;
                }
                continue;
            }
            list.push_back(std::move(prim));
        } else if (cmd->type() == Command::Type::Points) {
            const auto *pointsCmd = static_cast<const DrawPointsCommand *>(cmd.get());
            const DrawPointsData &d = pointsCmd->data();
            const std::size_t count = d.getPointCount();
            if (count == 0) {
                continue;
            }
            const float half = (d.size > 0.0f ? d.size : 1.0f) * 0.5f;
            wsc::DrawPrimitive prim;
            prim.kind = wsc::DrawPrimitiveKind::SolidTriangles;
            prim.blendMode = mapBlend(d.blendMode);
            prim.positions.reserve(count * 12);
            for (std::size_t i = 0; i < count; ++i) {
                const float cx = d.points[i * 2 + 0];
                const float cy = d.points[i * 2 + 1];
                emitQuad(prim.positions, d.transform, cx - half, cy - half, cx + half, cy - half, cx + half,
                         cy + half, cx - half, cy + half);
            }
            prim.color[0] = d.color[0];
            prim.color[1] = d.color[1];
            prim.color[2] = d.color[2];
            prim.color[3] = d.color[3];
            if (d.clipMask.hasPaths() && !emitClippedSolid(prim, d.clipMask)) {
                continue;
            }
            list.push_back(std::move(prim));
        } else if (cmd->type() == Command::Type::Lines) {
            const auto *linesCmd = static_cast<const DrawLinesCommand *>(cmd.get());
            const DrawLinesData &d = linesCmd->data();
            const std::size_t lineCount = d.getLineCount();
            if (lineCount == 0) {
                continue;
            }
            const float halfWidth = (d.width > 0.0f ? d.width : 1.0f) * 0.5f;
            wsc::DrawPrimitive prim;
            prim.kind = wsc::DrawPrimitiveKind::SolidTriangles;
            prim.blendMode = mapBlend(d.blendMode);
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
                emitQuad(prim.positions, d.transform, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny,
                         x0 - nx, y0 - ny);
            }
            if (prim.positions.empty()) {
                continue;
            }
            prim.color[0] = d.color[0];
            prim.color[1] = d.color[1];
            prim.color[2] = d.color[2];
            prim.color[3] = d.color[3];
            if (d.clipMask.hasPaths() && !emitClippedSolid(prim, d.clipMask)) {
                continue;
            }
            list.push_back(std::move(prim));
        } else if (cmd->type() == Command::Type::Image) {
            const auto *imageCmd = static_cast<const DrawImageCommand *>(cmd.get());
            const DrawImageData &d = imageCmd->data();
            if (!d.imageResource) {
                continue;
            }
            float nx[4], ny[4];
            toNdc(d.transform, d.x, d.y, nx[0], ny[0]);
            toNdc(d.transform, d.x + d.width, d.y, nx[1], ny[1]);
            toNdc(d.transform, d.x + d.width, d.y + d.height, nx[2], ny[2]);
            toNdc(d.transform, d.x, d.y + d.height, nx[3], ny[3]);
            const float uu[4] = {d.u0, d.u1, d.u1, d.u0};
            const float vv[4] = {d.v0, d.v0, d.v1, d.v1};
            const int idx[6] = {0, 1, 2, 0, 2, 3};
            wsc::DrawPrimitive prim;
            prim.kind = wsc::DrawPrimitiveKind::TexturedQuad;
            prim.blendMode = mapBlend(d.blendMode);
            prim.texture = d.imageResource;
            prim.layerAlpha = d.alpha;
            prim.tint[0] = d.tintColor[0];
            prim.tint[1] = d.tintColor[1];
            prim.tint[2] = d.tintColor[2];
            prim.tint[3] = d.tintColor[3];
            if (d.hasColorMatrix) {
                prim.hasColorMatrix = true;
                std::memcpy(prim.colorMatrix, d.colorMatrix, sizeof(prim.colorMatrix));
                std::memcpy(prim.colorMatrixOffset, d.colorMatrixOffset, sizeof(prim.colorMatrixOffset));
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
            if (d.clipMask.hasPaths()) {
                // Clipped image: render the textured quad in isolation, then
                // multiply its alpha by the clip mask before compositing.
                if (!emitClippedLayer(std::move(prim), d.clipMask, mapBlend(d.blendMode))) {
                    continue;
                }
                continue;
            }
            list.push_back(std::move(prim));
        } else if (cmd->type() == Command::Type::Text) {
            // Text is rendered as vector triangle geometry (no glyph atlas): the
            // glyph outlines are tessellated into local-space triangles that are
            // filled with a solid color or the same shader gradient as paths. The
            // GL shader evaluates the gradient in raw (pre-transform) local space
            // (vLocalPos = aPos), so we mirror that exactly.
            const auto *textCmd = static_cast<const DrawTextCommand *>(cmd.get());
            const DrawTextData &d = textCmd->data();
            const std::size_t vertexCount = d.getVertexCount();
            if (vertexCount < 3 || (vertexCount % 3) != 0) {
                continue;
            }
            const bool textClipped = d.clipMask.hasPaths();
            wsc::DrawPrimitive prim;
            prim.blendMode = mapBlend(d.blendMode);
            prim.positions.reserve(vertexCount * 2);
            for (std::size_t i = 0; i < vertexCount; ++i) {
                float nx = 0.0f, ny = 0.0f;
                toNdc(d.transform, d.vertices[i * 2 + 0], d.vertices[i * 2 + 1], nx, ny);
                prim.positions.push_back(nx);
                prim.positions.push_back(ny);
            }
            if (d.hasShaderGradient()) {
                prim.kind = wsc::DrawPrimitiveKind::GradientFill;
                // Gradient is evaluated in raw local glyph space (vLocalPos = aPos).
                prim.localPositions.assign(d.vertices.begin(),
                                           d.vertices.begin() + static_cast<std::ptrdiff_t>(vertexCount * 2));
                prim.gradientType = static_cast<int>(d.gradientType);
                prim.gradientTileMode = static_cast<int>(d.gradientTileMode);
                prim.linearStart[0] = d.gradientStart[0];
                prim.linearStart[1] = d.gradientStart[1];
                prim.linearEnd[0] = d.gradientEnd[0];
                prim.linearEnd[1] = d.gradientEnd[1];
                prim.radialCenter[0] = d.radialCenter[0];
                prim.radialCenter[1] = d.radialCenter[1];
                prim.radialRadius = d.radialRadius;
                prim.gradientStopCount = d.gradientStopCount;
                for (int i = 0; i < d.gradientStopCount && i < 8; ++i) {
                    prim.gradientStopPositions[i] = d.gradientStopPositions[i];
                    for (int c = 0; c < 4; ++c) {
                        prim.gradientStopColors[i * 4 + c] = d.gradientStopColors[i * 4 + c];
                    }
                }
            } else {
                prim.kind = wsc::DrawPrimitiveKind::SolidTriangles;
                prim.color[0] = d.color[0];
                prim.color[1] = d.color[1];
                prim.color[2] = d.color[2];
                prim.color[3] = d.color[3];
            }
            if (textClipped) {
                if (d.hasShaderGradient()) {
                    if (!emitClippedLayer(std::move(prim), d.clipMask, mapBlend(d.blendMode))) {
                        continue;
                    }
                    continue;
                }
                if (!emitClippedSolid(prim, d.clipMask)) {
                    continue;
                }
                list.push_back(std::move(prim));
                continue;
            }
            list.push_back(std::move(prim));
        } else if (cmd->type() == Command::Type::Shadow) {
            // Gaussian drop shadow. Mirrors DrawShadowCommand::execute: render a
            // white silhouette into an offscreen coverage target, separable
            // Gaussian blur it, then composite the tinted blurred coverage. The
            // blur is evaluated on the CPU (identical kernel math to the GL
            // separable passes) and the composite is expressed as a textured
            // full-canvas quad -- the textured shader computes
            // (tint.rgb, coverage * tint.a), exactly the GL composite formula --
            // so it blends in-order with the rest of the command stream.
            const auto *shadowCmd = static_cast<const DrawShadowCommand *>(cmd.get());
            const DrawShadowData &d = shadowCmd->data();
            const int sw = d.canvasWidth;
            const int sh = d.canvasHeight;
            if (sw <= 0 || sh <= 0 || !(d.blurRadius > 0.0f)) {
                continue;
            }
            const bool hasImageSilhouette = !d.imageSilhouette.empty();
            const DrawPathData &sil = d.silhouette;
            const std::size_t silCount = sil.getPointCount();
            if (!hasImageSilhouette && (silCount < 3 || (silCount % 3) != 0)) {
                continue;
            }

            // 1. Render the white silhouette into an offscreen coverage target.
            std::unique_ptr<IRenderTarget> covTarget = createRenderTarget(sw, sh);
            if (!covTarget || !covTarget->isValid()) {
                continue;
            }
            auto toNdcS = [&](const glm::mat4 &tf, float x, float y, float &ox, float &oy) {
                const glm::vec4 p = tf * glm::vec4(x, y, 0.0f, 1.0f);
                ox = p.x / static_cast<float>(sw) * 2.0f - 1.0f;
                oy = p.y / static_cast<float>(sh) * 2.0f - 1.0f;
            };
            wsc::DrawList silList;
            if (hasImageSilhouette) {
                // Bitmap / glyph-atlas text: the silhouette is a set of textured
                // quads whose sampled alpha is the glyph coverage. Draw them so
                // their alpha accumulates in the coverage target.
                for (const DrawImageData &gi : d.imageSilhouette) {
                    if (!gi.imageResource) {
                        continue;
                    }
                    float gnx[4], gny[4];
                    toNdcS(gi.transform, gi.x, gi.y, gnx[0], gny[0]);
                    toNdcS(gi.transform, gi.x + gi.width, gi.y, gnx[1], gny[1]);
                    toNdcS(gi.transform, gi.x + gi.width, gi.y + gi.height, gnx[2], gny[2]);
                    toNdcS(gi.transform, gi.x, gi.y + gi.height, gnx[3], gny[3]);
                    const float gu[4] = {gi.u0, gi.u1, gi.u1, gi.u0};
                    const float gv[4] = {gi.v0, gi.v0, gi.v1, gi.v1};
                    const int gidx[6] = {0, 1, 2, 0, 2, 3};
                    wsc::DrawPrimitive gp;
                    gp.kind = wsc::DrawPrimitiveKind::TexturedQuad;
                    gp.blendMode = 0; // SrcOver over transparent
                    gp.texture = gi.imageResource;
                    gp.layerAlpha = gi.alpha;
                    gp.tint[0] = 1.0f;
                    gp.tint[1] = 1.0f;
                    gp.tint[2] = 1.0f;
                    gp.tint[3] = 1.0f;
                    gp.sampling = static_cast<int>(gi.sampling);
                    gp.tileMode = static_cast<int>(gi.tileMode);
                    gp.useCustomSampler = true;
                    gp.positions.reserve(12);
                    gp.uvs.reserve(12);
                    for (int k : gidx) {
                        gp.positions.push_back(gnx[k]);
                        gp.positions.push_back(gny[k]);
                        gp.uvs.push_back(gu[k]);
                        gp.uvs.push_back(gv[k]);
                    }
                    silList.push_back(std::move(gp));
                }
            } else {
                wsc::DrawPrimitive sp;
                sp.kind = wsc::DrawPrimitiveKind::SolidTriangles;
                sp.blendMode = 0; // SrcOver over the cleared (transparent) target
                sp.positions.reserve(silCount * 2);
                for (std::size_t i = 0; i < silCount; ++i) {
                    float nx = 0.0f, ny = 0.0f;
                    toNdcS(sil.transform, sil.points[i * 2 + 0], sil.points[i * 2 + 1], nx, ny);
                    sp.positions.push_back(nx);
                    sp.positions.push_back(ny);
                }
                sp.color[0] = 1.0f;
                sp.color[1] = 1.0f;
                sp.color[2] = 1.0f;
                sp.color[3] = 1.0f;
                silList.push_back(std::move(sp));
            }
            if (silList.empty() || !executeDrawList(covTarget, silList)) {
                continue;
            }

            // 2. Read back the silhouette coverage (the alpha channel).
            std::vector<unsigned char> cov;
            if (!readPixelsRGBA(sw, sh, cov)) {
                continue;
            }
            const std::size_t pixelCount = static_cast<std::size_t>(sw) * static_cast<std::size_t>(sh);

            // 3. Separable Gaussian blur on the alpha channel (horizontal then
            //    vertical), edges clamped -- matching the GL CLAMP_TO_EDGE passes.
            const wsc::render::GaussianKernel kernel = wsc::render::computeGaussianKernel(d.blurRadius);
            const int radius = kernel.radius();
            std::vector<float> srcA(pixelCount);
            for (std::size_t i = 0; i < pixelCount; ++i) {
                srcA[i] = static_cast<float>(cov[i * 4 + 3]) / 255.0f;
            }
            std::vector<float> tmpA(pixelCount, 0.0f);
            std::vector<float> outA(pixelCount, 0.0f);
            auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
            for (int y = 0; y < sh; ++y) {
                for (int x = 0; x < sw; ++x) {
                    float a = srcA[static_cast<std::size_t>(y) * sw + x] * kernel.weights[0];
                    for (int k = 1; k <= radius; ++k) {
                        const int xl = clampi(x - k, 0, sw - 1);
                        const int xr = clampi(x + k, 0, sw - 1);
                        const float wk = kernel.weights[static_cast<std::size_t>(k)];
                        a += srcA[static_cast<std::size_t>(y) * sw + xl] * wk;
                        a += srcA[static_cast<std::size_t>(y) * sw + xr] * wk;
                    }
                    tmpA[static_cast<std::size_t>(y) * sw + x] = a;
                }
            }
            for (int y = 0; y < sh; ++y) {
                for (int x = 0; x < sw; ++x) {
                    float a = tmpA[static_cast<std::size_t>(y) * sw + x] * kernel.weights[0];
                    for (int k = 1; k <= radius; ++k) {
                        const int yt = clampi(y - k, 0, sh - 1);
                        const int yb = clampi(y + k, 0, sh - 1);
                        const float wk = kernel.weights[static_cast<std::size_t>(k)];
                        a += tmpA[static_cast<std::size_t>(yt) * sw + x] * wk;
                        a += tmpA[static_cast<std::size_t>(yb) * sw + x] * wk;
                    }
                    outA[static_cast<std::size_t>(y) * sw + x] = a;
                }
            }

            // 4. Upload the blurred coverage as a white-RGB / coverage-alpha texture.
            std::vector<unsigned char> shadowPixels(pixelCount * 4);
            for (std::size_t i = 0; i < pixelCount; ++i) {
                const float a = outA[i] < 0.0f ? 0.0f : (outA[i] > 1.0f ? 1.0f : outA[i]);
                shadowPixels[i * 4 + 0] = 255;
                shadowPixels[i * 4 + 1] = 255;
                shadowPixels[i * 4 + 2] = 255;
                shadowPixels[i * 4 + 3] = static_cast<unsigned char>(a * 255.0f + 0.5f);
            }
            SharedImageResource shadowTex =
                createSampledTexture(context_.get(), sw, sh, shadowPixels.data(), /*nearest=*/false);
            if (!shadowTex) {
                continue;
            }

            // 5. Composite as a tinted full-canvas textured quad (in stream order).
            float qnx[4], qny[4];
            toNdc(glm::mat4(1.0f), 0.0f, 0.0f, qnx[0], qny[0]);
            toNdc(glm::mat4(1.0f), w, 0.0f, qnx[1], qny[1]);
            toNdc(glm::mat4(1.0f), w, h, qnx[2], qny[2]);
            toNdc(glm::mat4(1.0f), 0.0f, h, qnx[3], qny[3]);
            const float qu[4] = {0.0f, 1.0f, 1.0f, 0.0f};
            const float qv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            const int qidx[6] = {0, 1, 2, 0, 2, 3};
            wsc::DrawPrimitive prim;
            prim.kind = wsc::DrawPrimitiveKind::TexturedQuad;
            prim.blendMode = mapBlend(d.blendMode);
            prim.texture = shadowTex;
            prim.layerAlpha = 1.0f;
            prim.tint[0] = d.color[0];
            prim.tint[1] = d.color[1];
            prim.tint[2] = d.color[2];
            prim.tint[3] = d.color[3];
            prim.sampling = static_cast<int>(DrawImageSampling::Linear);
            prim.tileMode = static_cast<int>(DrawImageTileMode::Clamp);
            prim.useCustomSampler = true;
            prim.positions.reserve(12);
            prim.uvs.reserve(12);
            for (int k : qidx) {
                prim.positions.push_back(qnx[k]);
                prim.positions.push_back(qny[k]);
                prim.uvs.push_back(qu[k]);
                prim.uvs.push_back(qv[k]);
            }
            list.push_back(std::move(prim));
        }
        // Other command kinds (gradients, clip) are ADR-006 follow-ups.
    }

    if (list.empty()) {
        return false;
    }
    return executeDrawList(target, list);
#else
    (void)target;
    (void)commands;
    (void)request;
    return false;
#endif
}
