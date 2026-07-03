#include "VulkanRenderDevice.h"

#include "../IRenderTarget.h"
#include "../IRenderer.h"

#include <iostream>

#if defined(WHATSCANVAS_ENABLE_VULKAN)
#include <array>
#include <cstring>
#include <optional>

#include <vulkan/vulkan.h>

#include "shaders/SolidShaderSpv.h"
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

    // M3 solid-color graphics pipeline (lazily created against a render pass).
    VkShaderModule solidVertModule = VK_NULL_HANDLE;
    VkShaderModule solidFragModule = VK_NULL_HANDLE;
    VkPipelineLayout solidPipelineLayout = VK_NULL_HANDLE;
    VkPipeline solidPipeline = VK_NULL_HANDLE;

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
            if (solidPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, solidPipeline, nullptr);
                solidPipeline = VK_NULL_HANDLE;
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

    // Lazily build the solid-color pipeline. All render targets use structurally
    // identical (compatible) render passes, so a single pipeline is reused.
    bool ensureSolidPipeline(VkRenderPass renderPass)
    {
        if (solidPipeline != VK_NULL_HANDLE) {
            return true;
        }

        solidVertModule = createShaderModule(kSolidVertSpv, sizeof(kSolidVertSpv));
        solidFragModule = createShaderModule(kSolidFragSpv, sizeof(kSolidFragSpv));
        if (solidVertModule == VK_NULL_HANDLE || solidFragModule == VK_NULL_HANDLE) {
            return false;
        }

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &solidPipelineLayout) != VK_SUCCESS) {
            return false;
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

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                         | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

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

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &solidPipeline)
            != VK_SUCCESS) {
            return false;
        }
        return true;
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
#if defined(WHATSCANVAS_ENABLE_VULKAN)
    if (!context_ || !context_->deviceReady || !target || ndcPositions.size() < 6
        || (ndcPositions.size() % 6) != 0) {
        return false;
    }
    auto *rt = dynamic_cast<VulkanRenderTarget *>(target.get());
    if (rt == nullptr || !rt->isValid()) {
        return false;
    }
    if (!context_->ensureSolidPipeline(rt->renderPass())) {
        return false;
    }

    // Build interleaved {x, y, r, g, b, a} vertices in a host-visible buffer.
    const std::size_t vertexCount = ndcPositions.size() / 2;
    std::vector<float> vertexData;
    vertexData.reserve(vertexCount * 6);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        vertexData.push_back(ndcPositions[i * 2 + 0]);
        vertexData.push_back(ndcPositions[i * 2 + 1]);
        vertexData.push_back(r);
        vertexData.push_back(g);
        vertexData.push_back(b);
        vertexData.push_back(a);
    }
    const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(vertexData.size()) * sizeof(float);

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    if (!context_->createHostVisibleBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer,
                                           vertexMemory)) {
        return false;
    }
    void *mapped = nullptr;
    if (vkMapMemory(context_->device, vertexMemory, 0, vertexBytes, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(context_->device, vertexBuffer, nullptr);
        vkFreeMemory(context_->device, vertexMemory, nullptr);
        return false;
    }
    std::memcpy(mapped, vertexData.data(), static_cast<std::size_t>(vertexBytes));
    vkUnmapMemory(context_->device, vertexMemory);

    VkCommandBuffer cmd = context_->beginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) {
        vkDestroyBuffer(context_->device, vertexBuffer, nullptr);
        vkFreeMemory(context_->device, vertexMemory, nullptr);
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
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(rt->width());
    viewport.height = static_cast<float>(rt->height());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<std::uint32_t>(rt->width()), static_cast<std::uint32_t>(rt->height())};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, context_->solidPipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
    vkCmdDraw(cmd, static_cast<std::uint32_t>(vertexCount), 1, 0, 0);

    vkCmdEndRenderPass(cmd);
    const bool submitted = context_->endSingleTimeCommands(cmd);

    vkDestroyBuffer(context_->device, vertexBuffer, nullptr);
    vkFreeMemory(context_->device, vertexMemory, nullptr);

    if (!submitted) {
        return false;
    }

    // Render pass finalLayout leaves the image ready for a transfer read.
    rt->setLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    rt->markReadbackReady();
    context_->readbackImage = rt->image();
    context_->readbackLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    context_->readbackWidth = rt->width();
    context_->readbackHeight = rt->height();
    return true;
#else
    (void)target;
    (void)ndcPositions;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
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
    }
#endif
    return stats;
}

// ---------------------------------------------------------------------------
// Resource + command entry points (later milestones)
// ---------------------------------------------------------------------------

SharedClipMaskResource VulkanRenderDevice::createClipMaskResource(const ClipMaskPath & /*maskPath*/) const
{
    // TODO(vulkan, M7): implement coverage-mask clip resources.
    return nullptr;
}

SharedImageResource VulkanRenderDevice::createImageResourceRGBA(int /*width*/, int /*height*/,
                                                                const std::vector<unsigned char> & /*pixels*/) const
{
    // TODO(vulkan, M5): implement image/texture resources.
    return nullptr;
}

SharedImageResource VulkanRenderDevice::createImageResourceFromImageData(int /*width*/, int /*height*/,
                                                                         int /*channels*/,
                                                                         const unsigned char * /*pixels*/,
                                                                         bool /*generateMipmaps*/) const
{
    // TODO(vulkan, M5): implement image/texture resources.
    return nullptr;
}

bool VulkanRenderDevice::updateImageResourceRGBA(const SharedImageResource & /*imageResource*/, int /*x*/, int /*y*/,
                                                 int /*width*/, int /*height*/, const unsigned char * /*pixels*/,
                                                 bool /*regenerateMipmaps*/) const
{
    // TODO(vulkan, M5): implement partial image updates.
    return false;
}

SharedImageResource VulkanRenderDevice::wrapExternalImageResource(ImageResourceHandle /*handle*/) const
{
    // TODO(vulkan, M8): implement external image wrapping.
    return nullptr;
}

SharedImageResource VulkanRenderDevice::renderCommandsToImageResource(
    const std::vector<std::unique_ptr<Command>> & /*commands*/, const OffscreenRenderRequest & /*request*/) const
{
    // TODO(vulkan, M6): implement command replay into an offscreen image.
    return nullptr;
}
