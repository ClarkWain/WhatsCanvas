#include "VulkanRenderDevice.h"

#include "../IRenderTarget.h"
#include "../IRenderer.h"
#include "command/DrawCommand.h"

#include <glm/glm.hpp>

#include <iostream>

#if defined(WHATSCANVAS_ENABLE_VULKAN)
#include <array>
#include <cmath>
#include <cstring>
#include <optional>

#include <vulkan/vulkan.h>

#include "shaders/SolidShaderSpv.h"
#include "shaders/TexturedShaderSpv.h"
#include "shaders/ClipShaderSpv.h"
#endif

// ---------------------------------------------------------------------------
// Compiled-in availability
// ---------------------------------------------------------------------------

bool VulkanRenderDevice::isAvailable()
{
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Backend context (M1 render core)
// ---------------------------------------------------------------------------

#if defined(WHATSCANVAS_ENABLE_VULKAN)

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
    VkPipeline texPipeline = VK_NULL_HANDLE;

    // M7 clip pipeline (position + color + mask UV; samples an R-channel mask).
    VkShaderModule clipVertModule = VK_NULL_HANDLE;
    VkShaderModule clipFragModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout clipDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout clipPipelineLayout = VK_NULL_HANDLE;
    VkPipeline clipPipeline = VK_NULL_HANDLE;

    std::size_t imageTextureCount = 0;

    // Most-recently-activated / filled render target, used as the readback
    // source for readPixelsRGBA(). Mirrors OpenGL's "current framebuffer".
    VkImage readbackImage = VK_NULL_HANDLE;
    VkImageLayout readbackLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    int readbackWidth = 0;
    int readbackHeight = 0;

    std::size_t renderTargetCount = 0;

    ~VulkanContext() { destroy(); }

    void destroy()
    {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            for (CachedPipeline &entry : pipelineCache) {
                if (entry.pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, entry.pipeline, nullptr);
                    entry.pipeline = VK_NULL_HANDLE;
                }
            }
            pipelineCache.clear();
            if (texPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, texPipeline, nullptr);
                texPipeline = VK_NULL_HANDLE;
            }
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
            if (clipPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, clipPipeline, nullptr);
                clipPipeline = VK_NULL_HANDLE;
            }
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
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                               | VK_COLOR_COMPONENT_A_BIT;
        switch (blendMode) {
        case 1: // Src
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            break;
        case 2: // Add
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case 3: // Multiply
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            break;
        case 4: // Screen
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            break;
        case 0: // SrcOver
        default:
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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
        binding.stride = 6 * sizeof(float);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attrs{};
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = 0;
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[1].offset = 2 * sizeof(float);

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

    // Lazily build the textured-quad pipeline (combined image sampler at
    // binding 0), reused across compatible render passes.
    bool ensureTexturePipeline(VkRenderPass renderPass)
    {
        if (texPipeline != VK_NULL_HANDLE) {
            return true;
        }

        texVertModule = createShaderModule(kTexturedVertSpv, sizeof(kTexturedVertSpv));
        texFragModule = createShaderModule(kTexturedFragSpv, sizeof(kTexturedFragSpv));
        if (texVertModule == VK_NULL_HANDLE || texFragModule == VK_NULL_HANDLE) {
            return false;
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
            return false;
        }

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &texDescriptorSetLayout;
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(float);
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &texPipelineLayout) != VK_SUCCESS) {
            return false;
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

        VkPipelineColorBlendAttachmentState blendAttachment = blendStateFor(0);

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

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &texPipeline) != VK_SUCCESS) {
            return false;
        }
        return true;
    }

    // Lazily build the clip pipeline (position + color + mask UV; combined image
    // sampler at binding 0). Used for coverage-mask clipping.
    bool ensureClipPipeline(VkRenderPass renderPass)
    {
        if (clipPipeline != VK_NULL_HANDLE) {
            return true;
        }

        clipVertModule = createShaderModule(kClipVertSpv, sizeof(kClipVertSpv));
        clipFragModule = createShaderModule(kClipFragSpv, sizeof(kClipFragSpv));
        if (clipVertModule == VK_NULL_HANDLE || clipFragModule == VK_NULL_HANDLE) {
            return false;
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
            return false;
        }

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &clipDescriptorSetLayout;
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &clipPipelineLayout) != VK_SUCCESS) {
            return false;
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

        VkPipelineColorBlendAttachmentState blendAttachment = blendStateFor(0);

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

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &clipPipeline)
            != VK_SUCCESS) {
            return false;
        }
        return true;
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
                          VkImageView view, VkSampler sampler, int width, int height)
        : context_(context),
          image_(image),
          memory_(memory),
          view_(view),
          sampler_(sampler),
          width_(width),
          height_(height)
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
        if (image_ != VK_NULL_HANDLE) vkDestroyImage(context_->device, image_, nullptr);
        if (memory_ != VK_NULL_HANDLE) vkFreeMemory(context_->device, memory_, nullptr);
        if (context_->imageTextureCount > 0) {
            --context_->imageTextureCount;
        }
    }

    bool isValid() const override
    {
        return image_ != VK_NULL_HANDLE && view_ != VK_NULL_HANDLE && sampler_ != VK_NULL_HANDLE;
    }

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

private:
    VulkanRenderDevice::VulkanContext *context_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    int width_ = 0;
    int height_ = 0;
};

// Create an owning sampled RGBA8 texture from tightly-packed pixels.
std::shared_ptr<VulkanTextureResource> createSampledTexture(VulkanRenderDevice::VulkanContext *ctx, int width,
                                                            int height, const unsigned char *pixels, bool nearest)
{
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
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
        VulkanRenderDevice::VulkanContext::transitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                                                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ctx->copyBufferToImage(cmd, staging, image, 0, 0, width, height);
        VulkanRenderDevice::VulkanContext::transitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    samplerInfo.minFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        cleanup();
        return nullptr;
    }

    return std::make_shared<VulkanTextureResource>(ctx, image, memory, view, sampler, width, height);
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
                       VkDeviceMemory memory, VkImageView view, VkRenderPass renderPass, VkFramebuffer framebuffer)
        : context_(context),
          width_(width),
          height_(height),
          image_(image),
          memory_(memory),
          view_(view),
          renderPass_(renderPass),
          framebuffer_(framebuffer),
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
        vkDeviceWaitIdle(context_->device);
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
        if (image_ != VK_NULL_HANDLE) {
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
    VkImageLayout layout() const { return layout_; }
    void setLayout(VkImageLayout layout) { layout_ = layout; }
    VkRenderPass renderPass() const { return renderPass_; }
    VkFramebuffer framebuffer() const { return framebuffer_; }
    void markReadbackReady() { activated_ = true; }

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

// Interleave positions + a single solid color into a batch vertex array.
std::vector<float> buildSolidVertices(const std::vector<float> &ndcPositions, float r, float g, float b, float a)
{
    const std::size_t vertexCount = ndcPositions.size() / 2;
    std::vector<float> out;
    out.reserve(vertexCount * 6);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        out.push_back(ndcPositions[i * 2 + 0]);
        out.push_back(ndcPositions[i * 2 + 1]);
        out.push_back(r);
        out.push_back(g);
        out.push_back(b);
        out.push_back(a);
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

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&instanceInfo, nullptr, &context_->instance) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderDevice] Failed to create Vulkan instance." << std::endl;
        return;
    }

    std::uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(context_->instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::cerr << "[VulkanRenderDevice] No Vulkan-capable physical devices found." << std::endl;
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
        std::cerr << "[VulkanRenderDevice] No physical device with a graphics queue was found." << std::endl;
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

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.pEnabledFeatures = &deviceFeatures;

    if (vkCreateDevice(context_->physicalDevice, &deviceInfo, nullptr, &context_->device) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderDevice] Failed to create Vulkan logical device." << std::endl;
        return;
    }

    vkGetDeviceQueue(context_->device, context_->graphicsQueueFamily, 0, &context_->graphicsQueue);

    // M1 render core: command pool for single-time and per-frame submissions.
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context_->graphicsQueueFamily;
    if (vkCreateCommandPool(context_->device, &poolInfo, nullptr, &context_->commandPool) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderDevice] Failed to create Vulkan command pool." << std::endl;
        return;
    }

    context_->deviceReady = true;
    backendInitialized_ = true;

    std::cout << "[VulkanRenderDevice] Initialized on device: " << context_->physicalDeviceName << std::endl;
#else
    std::cerr << "[VulkanRenderDevice] Vulkan support is not compiled into this build. "
                 "Reconfigure with -DWHATSCANVAS_ENABLE_VULKAN=ON and a Vulkan SDK to enable it."
              << std::endl;
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
    batch.vertices.reserve(vertexCount * 6);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        batch.vertices.push_back(ndcPositions[i * 2 + 0]);
        batch.vertices.push_back(ndcPositions[i * 2 + 1]);
        batch.vertices.push_back(rgbaPerVertex[i * 4 + 0]);
        batch.vertices.push_back(rgbaPerVertex[i * 4 + 1]);
        batch.vertices.push_back(rgbaPerVertex[i * 4 + 2]);
        batch.vertices.push_back(rgbaPerVertex[i * 4 + 3]);
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
    if (!context_->ensureClipPipeline(dst->renderPass())) {
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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, context_->clipPipeline);
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
                                                                         bool /*generateMipmaps*/) const
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
    return createSampledTexture(context_.get(), width, height, rgba.data(), /*nearest=*/false);
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
    if (!context_->ensureTexturePipeline(rt->renderPass())) {
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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, context_->texPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, context_->texPipelineLayout, 0, 1, &set, 0,
                                nullptr);
        const float layerAlpha = 1.0f;
        vkCmdPushConstants(cmd, context_->texPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float),
                           &layerAlpha);
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

SharedImageResource VulkanRenderDevice::wrapExternalImageResource(ImageResourceHandle /*handle*/) const
{
    // TODO(vulkan, M8): implement external image wrapping.
    return nullptr;
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
    if (!context_->ensureTexturePipeline(dst->renderPass())) {
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
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, context_->texPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, context_->texPipelineLayout, 0, 1, &set, 0,
                                nullptr);
        vkCmdPushConstants(cmd, context_->texPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float),
                           &layerAlpha);
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
    std::vector<unsigned char> pixels;
    if (!readPixelsRGBA(w, h, pixels)) {
        return nullptr;
    }
    return createImageResourceRGBA(w, h, pixels);
#else
    (void)commands;
    (void)request;
    return nullptr;
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
    for (const wsc::DrawPrimitive &prim : drawList) {
        if (prim.kind == wsc::DrawPrimitiveKind::TexturedQuad) {
            ++texturedCount;
        } else if (prim.kind == wsc::DrawPrimitiveKind::ClipFill) {
            ++clipCount;
        }
    }

    if (texturedCount > 0 && !context_->ensureTexturePipeline(rt->renderPass())) {
        return false;
    }
    if (clipCount > 0 && !context_->ensureClipPipeline(rt->renderPass())) {
        return false;
    }

    VkDescriptorPool pool = VK_NULL_HANDLE;
    const std::uint32_t samplerSets = texturedCount + clipCount;
    if (samplerSets > 0) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = samplerSets;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = samplerSets;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
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
        float layerAlpha = 1.0f;
    };
    std::vector<RecordedDraw> draws;
    draws.reserve(drawList.size());

    bool ok = true;
    auto cleanup = [&]() {
        for (RecordedDraw &d : draws) {
            if (d.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, d.buffer, nullptr);
            if (d.memory != VK_NULL_HANDLE) vkFreeMemory(device, d.memory, nullptr);
        }
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
    };

    // Bind a texture resource to a freshly allocated descriptor set of the given
    // layout. Returns VK_NULL_HANDLE on failure.
    auto allocSampledSet = [&](VulkanTextureResource *tex, VkDescriptorSetLayout layout) -> VkDescriptorSet {
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
        imageDescriptor.sampler = tex->sampler();
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
            if (prim.colors.size() == vertexCount * 4) {
                vertices.reserve(vertexCount * 6);
                for (std::size_t v = 0; v < vertexCount; ++v) {
                    vertices.push_back(prim.positions[v * 2 + 0]);
                    vertices.push_back(prim.positions[v * 2 + 1]);
                    vertices.push_back(prim.colors[v * 4 + 0]);
                    vertices.push_back(prim.colors[v * 4 + 1]);
                    vertices.push_back(prim.colors[v * 4 + 2]);
                    vertices.push_back(prim.colors[v * 4 + 3]);
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
            draw.pipeline = context_->texPipeline;
            draw.descriptorLayout = context_->texPipelineLayout;
            draw.pushLayerAlpha = true;
            draw.layerAlpha = prim.layerAlpha;
            draw.descriptorSet = allocSampledSet(tex, context_->texDescriptorSetLayout);
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
        } else { // ClipFill
            auto *mask = dynamic_cast<VulkanTextureResource *>(prim.texture.get());
            if (mask == nullptr || !mask->isValid()) {
                ok = false;
                break;
            }
            draw.pipeline = context_->clipPipeline;
            draw.descriptorLayout = context_->clipPipelineLayout;
            draw.descriptorSet = allocSampledSet(mask, context_->clipDescriptorSetLayout);
            if (draw.descriptorSet == VK_NULL_HANDLE) {
                ok = false;
                break;
            }
            const float r = prim.color[0], g = prim.color[1], b = prim.color[2], a = prim.color[3];
            vertices = {
                -1.0f, -1.0f, r, g, b, a, 0.0f, 0.0f, 1.0f, -1.0f, r, g, b, a, 1.0f, 0.0f,
                1.0f,  1.0f,  r, g, b, a, 1.0f, 1.0f, -1.0f, -1.0f, r, g, b, a, 0.0f, 0.0f,
                1.0f,  1.0f,  r, g, b, a, 1.0f, 1.0f, -1.0f, 1.0f,  r, g, b, a, 0.0f, 1.0f,
            };
            draw.vertexCount = 6;
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
                    vkCmdPushConstants(cmd, d.descriptorLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float),
                                       &d.layerAlpha);
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
    if (!context_ || !context_->deviceReady || !target) {
        return false;
    }
    auto *rt = dynamic_cast<VulkanRenderTarget *>(target.get());
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

    // Map the Canvas blend model to the Vulkan solid blend-mode indices.
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
        default:
            return 0; // SrcOver
        }
    };

    // Project a canvas-space point through a transform to Vulkan NDC using the
    // same ortho(0,w,h,0) convention the OpenGL path uses.
    auto toNdc = [&](const glm::mat4 &tf, float x, float y, float &ox, float &oy) {
        const glm::vec4 p = tf * glm::vec4(x, y, 0.0f, 1.0f);
        ox = p.x / w * 2.0f - 1.0f;
        oy = p.y / h * 2.0f - 1.0f;
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

    // Translate the real Command stream into backend-neutral primitives.
    wsc::DrawList list;
    for (const std::unique_ptr<Command> &cmd : commands) {
        if (!cmd) {
            continue;
        }

        if (cmd->type() == Command::Type::Path) {
            const auto *pathCmd = static_cast<const DrawPathCommand *>(cmd.get());
            const DrawPathData &d = pathCmd->data();
            if (d.hasShaderGradient() || d.clipMask.hasPaths()) {
                continue;
            }
            const std::size_t vertexCount = d.getPointCount();
            if (vertexCount < 3 || (vertexCount % 3) != 0) {
                continue;
            }
            wsc::DrawPrimitive prim;
            prim.kind = wsc::DrawPrimitiveKind::SolidTriangles;
            prim.blendMode = mapBlend(d.blendMode);
            prim.positions.reserve(vertexCount * 2);
            for (std::size_t i = 0; i < vertexCount; ++i) {
                float nx = 0.0f, ny = 0.0f;
                toNdc(d.transform, d.points[i * 2 + 0], d.points[i * 2 + 1], nx, ny);
                prim.positions.push_back(nx);
                prim.positions.push_back(ny);
            }
            if (d.hasVertexColors()) {
                prim.colors = d.colors; // per-vertex RGBA (baked gradient / vertex colors)
            }
            prim.color[0] = d.color[0];
            prim.color[1] = d.color[1];
            prim.color[2] = d.color[2];
            prim.color[3] = d.color[3];
            list.push_back(std::move(prim));
        } else if (cmd->type() == Command::Type::Points) {
            const auto *pointsCmd = static_cast<const DrawPointsCommand *>(cmd.get());
            const DrawPointsData &d = pointsCmd->data();
            if (d.clipMask.hasPaths()) {
                continue;
            }
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
            list.push_back(std::move(prim));
        } else if (cmd->type() == Command::Type::Lines) {
            const auto *linesCmd = static_cast<const DrawLinesCommand *>(cmd.get());
            const DrawLinesData &d = linesCmd->data();
            if (d.clipMask.hasPaths()) {
                continue;
            }
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
            list.push_back(std::move(prim));
        } else if (cmd->type() == Command::Type::Image) {
            const auto *imageCmd = static_cast<const DrawImageCommand *>(cmd.get());
            const DrawImageData &d = imageCmd->data();
            if (!d.imageResource || d.clipMask.hasPaths()) {
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
            prim.texture = d.imageResource;
            prim.layerAlpha = d.alpha;
            prim.positions.reserve(12);
            prim.uvs.reserve(12);
            for (int k : idx) {
                prim.positions.push_back(nx[k]);
                prim.positions.push_back(ny[k]);
                prim.uvs.push_back(uu[k]);
                prim.uvs.push_back(vv[k]);
            }
            list.push_back(std::move(prim));
        }
        // Other command kinds (text, gradients, clip) are ADR-006 follow-ups.
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
