#pragma once

#include <cstdint>

#include "Export.h"

namespace wsc {

enum class ExternalImageBackend
{
	Unknown,
	OpenGL,
	Vulkan,
	Metal
};

struct OpenGLExternalImage
{
	std::uint32_t textureId = 0;
};

struct VulkanExternalImage
{
	void *image = nullptr;     // VkImage
	void *imageView = nullptr; // VkImageView
	void *sampler = nullptr;   // VkSampler, optional when the backend creates one
	int imageLayout = 0;       // VkImageLayout value, kept untyped for public header portability
};

struct MetalExternalImage
{
	void *texture = nullptr; // bridged id<MTLTexture>
	void *sampler = nullptr; // bridged id<MTLSamplerState>, optional
	void *device = nullptr;  // bridged id<MTLDevice>, optional validation hint
};

struct WSC_API ExternalImageDescriptor
{
	ExternalImageBackend backend = ExternalImageBackend::Unknown;
	int width = 0;
	int height = 0;
	bool mipmapsGenerated = false;
	bool ownsResource = false;
	OpenGLExternalImage openGL;
	VulkanExternalImage vulkan;
	MetalExternalImage metal;

	bool hasValidSize() const { return width > 0 && height > 0; }

	static ExternalImageDescriptor openGLTexture(std::uint32_t textureId, int width = 0, int height = 0,
	                                             bool mipmapsGenerated = false, bool ownsResource = false)
	{
		ExternalImageDescriptor desc;
		desc.backend = ExternalImageBackend::OpenGL;
		desc.width = width;
		desc.height = height;
		desc.mipmapsGenerated = mipmapsGenerated;
		desc.ownsResource = ownsResource;
		desc.openGL.textureId = textureId;
		return desc;
	}

	static ExternalImageDescriptor vulkanImage(void *image, void *imageView, void *sampler, int imageLayout,
	                                           int width, int height, bool mipmapsGenerated = false,
	                                           bool ownsResource = false)
	{
		ExternalImageDescriptor desc;
		desc.backend = ExternalImageBackend::Vulkan;
		desc.width = width;
		desc.height = height;
		desc.mipmapsGenerated = mipmapsGenerated;
		desc.ownsResource = ownsResource;
		desc.vulkan.image = image;
		desc.vulkan.imageView = imageView;
		desc.vulkan.sampler = sampler;
		desc.vulkan.imageLayout = imageLayout;
		return desc;
	}

	static ExternalImageDescriptor metalTexture(void *texture, void *sampler, void *device, int width, int height,
	                                            bool mipmapsGenerated = false, bool ownsResource = false)
	{
		ExternalImageDescriptor desc;
		desc.backend = ExternalImageBackend::Metal;
		desc.width = width;
		desc.height = height;
		desc.mipmapsGenerated = mipmapsGenerated;
		desc.ownsResource = ownsResource;
		desc.metal.texture = texture;
		desc.metal.sampler = sampler;
		desc.metal.device = device;
		return desc;
	}
};

} // namespace wsc
