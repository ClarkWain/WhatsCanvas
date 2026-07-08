// Verifies Vulkan wrap-external: WhatsCanvas renders into a host-owned VkImage
// (allocated on the canvas's own Vulkan device via the interop accessors), then
// reads it back and checks the drawn color. Vulkan-only; skips cleanly when no
// Vulkan device is present.

#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

#include <wsc/wsc.h>

#include <vulkan/vulkan.h>

using namespace wsc;

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
    }
    return condition;
}

std::optional<std::uint32_t> findMemoryType(VkPhysicalDevice pd, std::uint32_t typeBits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
    for (std::uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

int main()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Vulkan)) {
        std::cout << "VulkanWrapExternalTests: Vulkan unavailable; skipping." << std::endl;
        return 0;
    }

    const int W = 64;
    const int H = 48;
    std::unique_ptr<Canvas> canvas = Canvas::create(Canvas::Backend::Vulkan, W, H);
    if (!canvas) {
        return expect(false, "create(Vulkan) should succeed") ? 0 : 1;
    }
    canvas->initializeContext();

    auto pd = static_cast<VkPhysicalDevice>(canvas->vulkanPhysicalDevice());
    auto dev = static_cast<VkDevice>(canvas->vulkanDevice());
    if (pd == VK_NULL_HANDLE || dev == VK_NULL_HANDLE) {
        return expect(false, "vulkan interop handles should be non-null") ? 0 : 1;
    }

    // Host-owned image the canvas will render into.
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {static_cast<std::uint32_t>(W), static_cast<std::uint32_t>(H), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!expect(vkCreateImage(dev, &imageInfo, nullptr, &image) == VK_SUCCESS, "vkCreateImage")) {
        return 1;
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(dev, image, &memReq);
    const auto typeIndex = findMemoryType(pd, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!expect(typeIndex.has_value(), "device-local memory type")) {
        vkDestroyImage(dev, image, nullptr);
        return 1;
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = typeIndex.value();
    if (!expect(vkAllocateMemory(dev, &allocInfo, nullptr, &memory) == VK_SUCCESS, "vkAllocateMemory")) {
        vkDestroyImage(dev, image, nullptr);
        return 1;
    }
    vkBindImageMemory(dev, image, memory, 0);

    bool ok = true;

    const OutputTarget target = OutputTarget::VulkanImageTarget(
        reinterpret_cast<void *>(image), static_cast<unsigned long long>(VK_FORMAT_R8G8B8A8_UNORM), W, H);
    ok = expect(canvas->setOutputTarget(target), "setOutputTarget(VulkanImage)") && ok;

    canvas->beginFrame();
    Paint fill;
    fill.setColor(Color(255, 0, 0, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)), fill);
    canvas->flush();

    std::vector<unsigned char> pixels;
    ok = expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA from external image") && ok;
    if (pixels.size() == static_cast<std::size_t>(W) * H * 4u) {
        const std::size_t idx = (static_cast<std::size_t>(H / 2) * W + (W / 2)) * 4u;
        ok = expect(pixels[idx + 0] > 200 && pixels[idx + 1] < 60 && pixels[idx + 2] < 60,
                    "center pixel of the external image should be red") && ok;
    } else {
        ok = expect(false, "readback size should be W*H*4") && ok;
    }

    // Detach the external target and clean up the host image.
    canvas->setOutputTarget(OutputTarget::Offscreen());
    vkDeviceWaitIdle(dev);
    vkDestroyImage(dev, image, nullptr);
    vkFreeMemory(dev, memory, nullptr);

    if (!ok) {
        std::cerr << "VulkanWrapExternalTests: FAILED" << std::endl;
        return 1;
    }
    std::cout << "VulkanWrapExternalTests: all checks passed." << std::endl;
    return 0;
}
