#include <iostream>
#include <vector>
#include <string>
#include <optional>

#include "glad/glad.h"

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

using namespace std;

const int texWidth = 512, texHeight = 512;

#define CHECK_VK_RESULT(f) do { if (auto vkResult = (f); vkResult != VK_SUCCESS) { std::cerr << "Vulkan error " << vkResult << " at " << __FILE__ << ":" << __LINE__ << endl; abort(); } } while(false)

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
		if (typeFilter & (1 << i))
			return i;

	throw std::runtime_error("failed to find suitable memory type!");
}

void createBuffer(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	CHECK_VK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS);

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

	CHECK_VK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS);

	vkBindBufferMemory(device, buffer, bufferMemory, 0);
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

	// init vulkan instance
	VkInstance instance{};
	{
		VkInstanceCreateInfo instanceCreateInfo{};
		instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions;

		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		instanceCreateInfo.enabledExtensionCount = glfwExtensionCount;
		instanceCreateInfo.ppEnabledExtensionNames = glfwExtensions;

		CHECK_VK_RESULT(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));
	}

	// vulkan physical device
	VkPhysicalDevice physicalDevice{};
	{
		uint32_t deviceCount = 0;
		CHECK_VK_RESULT(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
		if (deviceCount == 0) {
			cerr << "Failed to find GPUs with Vulkan support!" << endl;
			return -1;
		}
		vector<VkPhysicalDevice> devices(deviceCount);
		CHECK_VK_RESULT(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));

		// pick the discrete gpu if available
		for (const auto& device : devices) {
			VkPhysicalDeviceProperties deviceProperties;
			vkGetPhysicalDeviceProperties(device, &deviceProperties);
			if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
				physicalDevice = device;
				break;
			}
		}
		if (physicalDevice == VK_NULL_HANDLE) {
			physicalDevice = devices[0];
		}
	}

	// queue family indices
	optional<uint32_t> graphicsQueueFamilyIndex;
	{
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

		int i = 0;
		for (const auto& queueFamily : queueFamilies)
		{
			if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				graphicsQueueFamilyIndex = i;
				break;
			}

			++i;
		}

		if (!graphicsQueueFamilyIndex.has_value())
		{
			cerr << "Could not find graphics queue family." << endl;
			return -1;
		}
	}

	// vulkan logical device 
	VkDevice device{};
	VkQueue graphicsQueue{};
	{
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex.value();
		queueCreateInfo.queueCount = 1;

		float queuePriority = 1.0f;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		VkPhysicalDeviceFeatures deviceFeatures{};

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = 0;
		createInfo.enabledLayerCount = 0; // validation layers go here

		CHECK_VK_RESULT(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device));
		vkGetDeviceQueue(device, graphicsQueueFamilyIndex.value(), 0, &graphicsQueue);
	}

	// create shared texture
	VkImage vulkanImage{};
	VkDeviceMemory vulkanImageMemory{};
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = texWidth;
		imageInfo.extent.height = texHeight;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
		imageInfo.tiling = VK_IMAGE_TILING_LINEAR; // optimal?
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // VK_IMAGE_USAGE_SAMPLED_BIT?
		imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT; // VK_SHARING_MODE_EXCLUSIVE?

		CHECK_VK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &vulkanImage));

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device, vulkanImage, &memRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		CHECK_VK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &vulkanImageMemory) != VK_SUCCESS);
		CHECK_VK_RESULT(vkBindImageMemory(device, vulkanImage, vulkanImageMemory, 0));
	}

	// get OpenGL texture from the vulkan shared texture
	auto vkGetMemoryWin32HandleKHR2 = PFN_vkGetMemoryWin32HandleKHR(vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandleKHR"));

	// main loop
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}
}
