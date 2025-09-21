#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <ranges>
#include <sstream>

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

#ifdef _DEBUG
VKAPI_ATTR vk::Bool32 VKAPI_CALL debugMessageFunc(vk::DebugUtilsMessageSeverityFlagBitsEXT       messageSeverity,
	vk::DebugUtilsMessageTypeFlagsEXT              messageTypes,
	vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData,
	void* /*pUserData*/)
{
	std::ostringstream message;

	message << vk::to_string(messageSeverity) << ": " << vk::to_string(messageTypes) << ":\n";
	message << std::string("\t") << "messageIDName   = <" << pCallbackData->pMessageIdName << ">\n";
	message << std::string("\t") << "messageIdNumber = " << pCallbackData->messageIdNumber << "\n";
	message << std::string("\t") << "message         = <" << pCallbackData->pMessage << ">\n";
	if (0 < pCallbackData->queueLabelCount)
	{
		message << std::string("\t") << "Queue Labels:\n";
		for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++)
		{
			message << std::string("\t\t") << "labelName = <" << pCallbackData->pQueueLabels[i].pLabelName << ">\n";
		}
	}
	if (0 < pCallbackData->cmdBufLabelCount)
	{
		message << std::string("\t") << "CommandBuffer Labels:\n";
		for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++)
		{
			message << std::string("\t\t") << "labelName = <" << pCallbackData->pCmdBufLabels[i].pLabelName << ">\n";
		}
	}
	if (0 < pCallbackData->objectCount)
	{
		message << std::string("\t") << "Objects:\n";
		for (uint32_t i = 0; i < pCallbackData->objectCount; i++)
		{
			message << std::string("\t\t") << "Object " << i << "\n";
			message << std::string("\t\t\t") << "objectType   = " << vk::to_string(pCallbackData->pObjects[i].objectType) << "\n";
			message << std::string("\t\t\t") << "objectHandle = " << pCallbackData->pObjects[i].objectHandle << "\n";
			if (pCallbackData->pObjects[i].pObjectName)
			{
				message << std::string("\t\t\t") << "objectName   = <" << pCallbackData->pObjects[i].pObjectName << ">\n";
			}
		}
	}

#ifdef _WIN32
	MessageBoxA(NULL, message.str().c_str(), "Alert", MB_OK);
#else
	std::cout << message.str() << std::endl;
#endif

	return false;
}
#endif

static void CheckShaderCompilation(GLuint shader)
{
	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		GLchar infoLog[1024];
		glGetShaderInfoLog(shader, 1024, NULL, infoLog);
		std::cerr << "Error compiling shader: " << infoLog << std::endl;
		abort();
	}
}

static void CheckProgramCompilation(GLuint program)
{
	GLint success;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success) {
		GLchar infoLog[1024];
		glGetProgramInfoLog(program, 1024, NULL, infoLog);
		std::cerr << "Error linking program: " << infoLog << std::endl;
		abort();
	}
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
#ifdef _DEBUG
	vk::DebugUtilsMessengerEXT debugUtilsMessenger;
#endif
	{
		vector<const char*> extensions = {
			VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
			VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
#ifdef _DEBUG
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
		};

		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
		for (uint32_t i = 0; i < glfwExtensionCount; ++i)
			extensions.push_back(glfwExtensions[i]);

		instance = vk::createInstance(vk::InstanceCreateInfo{
			.enabledExtensionCount = (uint32_t)extensions.size(),
			.ppEnabledExtensionNames = extensions.data()
			});
		VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

#ifdef _DEBUG
		debugUtilsMessenger = instance.createDebugUtilsMessengerEXT({
			.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
			.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
			.pfnUserCallback = debugMessageFunc });
#endif
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
			.pQueuePriorities = &queuePriority,
		};
		const char* deviceExtensions[] = {
			VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
			VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
			VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
			VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
		};
		device = physicalDevice.createDevice({
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queueCreateInfo,
			.enabledExtensionCount = 4,
			.ppEnabledExtensionNames = deviceExtensions,
			});
		//VULKAN_HPP_DEFAULT_DISPATCHER.init(instance, device);
		graphicsQueue = device.getQueue((uint32_t)queueFamilyIndex, 0);
	}

	// create shared texture
	vk::Image vulkanImage;
	vk::DeviceMemory vulkanImageMemory;
	vk::DeviceSize vulkanImageMemorySize;
	vk::CommandBuffer cmdBuffer;
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
		vulkanImageMemorySize = memoryRequirements.size;
		vk::ExportMemoryAllocateInfo exportInfo{
			.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32
		};
		vulkanImageMemory = device.allocateMemory({
			.pNext = &exportInfo,
			.allocationSize = memoryRequirements.size,
			.memoryTypeIndex = findMemoryType(physicalDevice, memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
			});
		device.bindImageMemory(vulkanImage, vulkanImageMemory, 0);

		cmdBuffer = device.allocateCommandBuffers({
			.commandPool = device.createCommandPool({
				.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
				.queueFamilyIndex = (uint32_t)queueFamilyIndex
				}),
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1
			})[0];

		// transition image layout
		cmdBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		vk::ImageMemoryBarrier imageBarrier{
			.srcAccessMask = {},
			.dstAccessMask = vk::AccessFlagBits::eTransferWrite,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::eTransferSrcOptimal,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = vulkanImage,
			.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
		};
		cmdBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
			{},
			0, nullptr,
			0, nullptr,
			1, &imageBarrier);
		cmdBuffer.end();
		graphicsQueue.submit({
			{.commandBufferCount = 1, .pCommandBuffers = &cmdBuffer  }
			});
	}

	// get shared handle to the Vulkan image
	auto hTextureMem = (HANDLE)device.getMemoryWin32HandleKHR({
		.memory = vulkanImageMemory,
		.handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32
		});

	// create opengl texture from the shared handle
	GLuint glTextureMemory;
	glCreateMemoryObjectsEXT(1, &glTextureMemory);
	glImportMemoryWin32HandleEXT(glTextureMemory, vulkanImageMemorySize, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, hTextureMem);

	GLuint glTexture;
	glCreateTextures(GL_TEXTURE_2D, 1, &glTexture);
	glTextureStorageMem2DEXT(glTexture, 1, GL_RGBA8, texWidth, texHeight, glTextureMemory, 0);

	// setup
	glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
	glViewport(0, 0, 800, 600);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, glTexture);
	glDisable(GL_DEPTH_TEST);

	// vertex buffer
	GLuint vbo;
	glGenBuffers(1, &vbo);

	// VAO
	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	float vertices[] = {
		-1, -1, 0, 0, 0,
		 1, -1, 0, 1, 0,
		 1,  1, 0, 1, 1,
		-1, -1, 0, 0, 0,
		 1,  1, 0, 1, 1,
		 -1,  1, 0, 0, 1
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(0);

	// vertex shader
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	const auto vertexShaderSource = R"(
		#version 330 core
		layout (location = 0) in vec3 aPos;
		layout (location = 1) in vec2 aUv;

		out vec2 vUv;

		void main()
		{
			gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
			vUv = aUv;
		})";
	glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
	glCompileShader(vertexShader);
	CheckShaderCompilation(vertexShader);

	// fragment shader
	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	const auto fragmentShaderSource = R"(
		#version 330 core

		in vec2 vUv;

		out vec4 FragColor;

		void main()
		{
			FragColor = vec4(vUv.xy, 0.0, 1.0);
		})";
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
	glCompileShader(fragmentShader);
	CheckShaderCompilation(fragmentShader);

	// shader program
	GLuint shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);
	CheckProgramCompilation(shaderProgram);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	// main loop
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		// render
		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(shaderProgram);
		glBindVertexArray(vao);
		glBindTexture(GL_TEXTURE_2D, glTexture);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		glfwSwapBuffers(window);
	}
}
