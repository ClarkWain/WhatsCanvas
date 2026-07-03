#include "VulkanRenderDevice.h"

#include "../IRenderTarget.h"

#include <iostream>

#if defined(WHATSCANVAS_ENABLE_VULKAN)
#include <cstring>
#include <optional>

#include <vulkan/vulkan.h>
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
// Backend context
// ---------------------------------------------------------------------------

#if defined(WHATSCANVAS_ENABLE_VULKAN)

struct VulkanRenderDevice::VulkanContext
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily = 0;
    std::string physicalDeviceName;
    bool deviceReady = false;

    ~VulkanContext()
    {
        if (device != VK_NULL_HANDLE) {
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
    }
};

namespace {

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
// Resource + command entry points (later milestones)
// ---------------------------------------------------------------------------

bool VulkanRenderDevice::readPixelsRGBA(int /*width*/, int /*height*/,
                                        std::vector<unsigned char> & /*pixels*/) const
{
    // TODO(vulkan): implement framebuffer readback once the render pass lands.
    return false;
}

std::unique_ptr<IRenderTarget> VulkanRenderDevice::createRenderTarget(int /*width*/, int /*height*/) const
{
    // TODO(vulkan): implement offscreen render targets.
    return nullptr;
}

SharedClipMaskResource VulkanRenderDevice::createClipMaskResource(const ClipMaskPath & /*maskPath*/) const
{
    // TODO(vulkan): implement coverage-mask clip resources.
    return nullptr;
}

SharedImageResource VulkanRenderDevice::createImageResourceRGBA(int /*width*/, int /*height*/,
                                                                const std::vector<unsigned char> & /*pixels*/) const
{
    // TODO(vulkan): implement image/texture resources.
    return nullptr;
}

SharedImageResource VulkanRenderDevice::createImageResourceFromImageData(int /*width*/, int /*height*/,
                                                                         int /*channels*/,
                                                                         const unsigned char * /*pixels*/,
                                                                         bool /*generateMipmaps*/) const
{
    // TODO(vulkan): implement image/texture resources.
    return nullptr;
}

bool VulkanRenderDevice::updateImageResourceRGBA(const SharedImageResource & /*imageResource*/, int /*x*/, int /*y*/,
                                                 int /*width*/, int /*height*/, const unsigned char * /*pixels*/,
                                                 bool /*regenerateMipmaps*/) const
{
    // TODO(vulkan): implement partial image updates.
    return false;
}

SharedImageResource VulkanRenderDevice::wrapExternalImageResource(ImageResourceHandle /*handle*/) const
{
    // TODO(vulkan): implement external image wrapping.
    return nullptr;
}

RenderResourceStats VulkanRenderDevice::resourceStats() const
{
    return RenderResourceStats{};
}

SharedImageResource VulkanRenderDevice::renderCommandsToImageResource(
    const std::vector<std::unique_ptr<Command>> & /*commands*/, const OffscreenRenderRequest & /*request*/) const
{
    // TODO(vulkan): implement command replay into an offscreen image.
    return nullptr;
}
