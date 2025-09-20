#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <ranges>

#include "glad/glad.h"

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

using namespace std;
namespace views = std::ranges::views;

const int texWidth = 512, texHeight = 512;

#define CHECK_VK_RESULT(f) do { if (auto vkResult = (f); vkResult != VK_SUCCESS) { std::cerr << "Vulkan error " << vkResult << " at " << __FILE__ << ":" << __LINE__ << endl; abort(); } } while(false)

static uint32_t findMemoryType(const vk::PhysicalDevice& physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties)
{
	auto memProperties = physicalDevice.getMemoryProperties();

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
		if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			return i;

	throw std::runtime_error("failed to find suitable memory type!");
}

static void createBuffer(const vk::PhysicalDevice& physicalDevice, const vk::Device& device, vk::DeviceSize size,
	vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
	buffer = device.createBuffer({
		.size = size,
		.usage = usage,
		.sharingMode = vk::SharingMode::eConcurrent  // exclusive?
		});

	auto memRequirements = device.getBufferMemoryRequirements(buffer);
	bufferMemory = device.allocateMemory({
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties)
		});
	device.bindBufferMemory(buffer, bufferMemory, 0);
}

int main()
{
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	auto window = glfwCreateWindow(800, 600, "Server", nullptr, nullptr);

	// init opengl
	glfwMakeContextCurrent(window);
	gladLoadGL();

	// vulkan dispatcher
	VULKAN_HPP_DEFAULT_DISPATCHER.init();

	// init vulkan instance
	vk::Instance instance;
	{
		vector<const char*> extensions = { /*VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME*/ };

		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
		for (uint32_t i = 0; i < glfwExtensionCount; ++i)
			extensions.push_back(glfwExtensions[i]);

		instance = vk::createInstance(vk::InstanceCreateInfo{
			.enabledExtensionCount = (uint32_t)extensions.size(),
			.ppEnabledExtensionNames = extensions.data()
			});
		VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
	}

	// vulkan physical device
	auto allPhysicalDevices = instance.enumeratePhysicalDevices();
	auto physicalDevice = *ranges::find_if(allPhysicalDevices,
		[](auto& d) {return d.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu; });

	// queue family indices
	auto allQueueFamilies = physicalDevice.getQueueFamilyProperties();
	auto queueFamilyIndex = distance(allQueueFamilies.begin(),
		std::find_if(allQueueFamilies.begin(), allQueueFamilies.end(),
			[](vk::QueueFamilyProperties const& qfp) { return qfp.queueFlags & vk::QueueFlagBits::eGraphics; }));

	// vulkan logical device 
	vk::Device device;
	vk::Queue graphicsQueue;
	{
		float queuePriority = 1.0f;
		vk::DeviceQueueCreateInfo queueCreateInfo{
			.queueFamilyIndex = (uint32_t)queueFamilyIndex,
			.queueCount = 1,
			.pQueuePriorities = &queuePriority
		};
		device = physicalDevice.createDevice({
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queueCreateInfo,
			});
		//VULKAN_HPP_DEFAULT_DISPATCHER.init(instance, device);
		graphicsQueue = device.getQueue((uint32_t)queueFamilyIndex, 0);
	}

	// create shared texture
	vk::Image vulkanImage;
	vk::DeviceMemory vulkanImageMemory;
	{
		vulkanImage = device.createImage({
			.imageType = vk::ImageType::e2D,
			.format = vk::Format::eR8G8B8A8Srgb,
			.extent = { (uint32_t)texWidth, (uint32_t)texHeight, 1 },
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = vk::SampleCountFlagBits::e1,
			.tiling = vk::ImageTiling::eLinear, // optimal?
			.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, // vk::ImageUsageFlagBits::eSampled?
			.sharingMode = vk::SharingMode::eConcurrent, // vk::SharingMode::eExclusive?
			.initialLayout = vk::ImageLayout::eUndefined,
			});

		auto memoryRequirements = device.getImageMemoryRequirements(vulkanImage);
		vk::ExportMemoryAllocateInfo exportInfo{
				.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32
		};
		vulkanImageMemory = device.allocateMemory({
			.pNext = &exportInfo,
			.allocationSize = memoryRequirements.size,
			.memoryTypeIndex = findMemoryType(physicalDevice, memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
			});
		device.bindImageMemory(vulkanImage, vulkanImageMemory, 0);
	}

	//// get OpenGL texture from the vulkan shared texture
	//VkMemoryGetWin32HandleInfoKHR info{
	//		.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
	//		.pNext = nullptr,
	//		.memory = vulkanImageMemory,
	//		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
	//};
	//HANDLE handle;
	//vk::detail::defaultDispatchLoaderDynamic.vkGetMemoryWin32HandleKHR(device, &info, &handle);
	auto hTextureMem = (HANDLE)device.getMemoryWin32HandleKHR({
		.memory = vulkanImageMemory,
		.handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32
		});

	// main loop
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}
}
