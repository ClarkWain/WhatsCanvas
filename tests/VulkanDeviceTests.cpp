// Real-hardware bring-up test for the Vulkan render backend.
//
// This test is only built when the project is configured with
// -DWHATSCANVAS_ENABLE_VULKAN=ON and a Vulkan SDK is available. It constructs a
// VulkanRenderDevice, initializes the backend, and verifies that a Vulkan
// logical device and graphics queue were created on the current machine.

#include <iostream>

#include "render/vulkan/VulkanRenderDevice.h"

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanDeviceTests] FAIL: Vulkan support was not compiled into the library." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();

    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanDeviceTests] FAIL: Vulkan logical device was not created." << std::endl;
        return 1;
    }

    if (device.selectedDeviceName().empty()) {
        std::cerr << "[VulkanDeviceTests] FAIL: selected physical device has no name." << std::endl;
        return 1;
    }

    std::cout << "[VulkanDeviceTests] PASS: initialized Vulkan on \"" << device.selectedDeviceName() << "\"."
              << std::endl;

    device.finalizeBackend();
    if (device.isDeviceReady()) {
        std::cerr << "[VulkanDeviceTests] FAIL: device still reports ready after finalize." << std::endl;
        return 1;
    }

    return 0;
}
