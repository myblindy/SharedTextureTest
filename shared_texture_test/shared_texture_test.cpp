#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <ranges>
#include <sstream>

#include "glad/glad.h"

#define VK_USE_PLATFORM_WIN32_KHR
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
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
	SDL_Init(SDL_INIT_VIDEO);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

#ifdef _DEBUG
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

	auto window = SDL_CreateWindow("Server", 800, 600, SDL_WINDOW_OPENGL /*| SDL_WINDOW_VULKAN*/);
	SDL_GL_SetSwapInterval(0);
	auto glContext = SDL_GL_CreateContext(window);

	// init opengl
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

		uint32_t sdlExtensionCount = 0;
		auto sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
		for (uint32_t i = 0; i < sdlExtensionCount; ++i)
			extensions.push_back(sdlExtensions[i]);

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
	vk::Semaphore vulkanImageTransitionCompleteSemaphore;
	GLuint glImageTransitionCompleteSemaphore;
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
			.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32 | vk::ExternalMemoryHandleTypeFlagBits::eD3D11Texture
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

		// create semaphore for layout transition
		vk::ExportSemaphoreCreateInfo exportSemaphoreCreateInfo{
			.handleTypes = vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32
		};
		vulkanImageTransitionCompleteSemaphore = device.createSemaphore({
			.pNext = &exportSemaphoreCreateInfo
			});
		auto hSemaphore = (HANDLE)device.getSemaphoreWin32HandleKHR({
			.semaphore = vulkanImageTransitionCompleteSemaphore,
			.handleType = vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32
			});
		glGenSemaphoresEXT(1, &glImageTransitionCompleteSemaphore);
		glImportSemaphoreWin32HandleEXT(glImageTransitionCompleteSemaphore, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, hSemaphore);

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
			{
				.commandBufferCount = 1,
				.pCommandBuffers = &cmdBuffer,
				.signalSemaphoreCount = 1,
				.pSignalSemaphores = &vulkanImageTransitionCompleteSemaphore,
			}
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
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

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
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

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
		uniform sampler2D sharedTexture;

		out vec4 FragColor;

		void main()
		{
			FragColor = texture(sharedTexture, vUv);
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

	assert(glGetError() == GL_NO_ERROR);

	// compute shader that draws expanding circles
	GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
	const auto computeShaderSource = R"(
		#version 430 core
		layout (local_size_x = 16, local_size_y = 16) in;

		layout (rgba8, binding = 0) uniform image2D img_output;
		layout (location = 1) uniform float time;

		void main() {
			ivec2 pixel_coords = ivec2(gl_GlobalInvocationID.xy);
			ivec2 img_size = imageSize(img_output);
			if (pixel_coords.x >= img_size.x || pixel_coords.y >= img_size.y) {
				return;
			}
			vec2 uv = vec2(pixel_coords) / vec2(img_size);
			vec2 center = vec2(0.5, 0.5);
			float dist = distance(uv, center);

			float color = pow(sin((dist + time / 20.0) * 40.0), 2.0);
			vec4 outColor = vec4(color, color, color, 1.0);
			imageStore(img_output, pixel_coords, outColor);
		})";
	glShaderSource(computeShader, 1, &computeShaderSource, nullptr);
	glCompileShader(computeShader);
	CheckShaderCompilation(computeShader);

	GLuint computeProgram = glCreateProgram();
	glAttachShader(computeProgram, computeShader);
	glLinkProgram(computeProgram);
	CheckProgramCompilation(computeProgram);
	glDeleteShader(computeShader);

	// wait (asynchroneously) for the vulkan image layout transition to complete
	glWaitSemaphoreEXT(glImageTransitionCompleteSemaphore, 0, nullptr, 0, nullptr, nullptr);

	assert(glGetError() == GL_NO_ERROR);

	{
		// create a pipe for the client to inherit the shared texture handle
		constexpr auto pipeName = LR"(\\.\pipe\SharedTextureTestPipe)";
		auto hPipe = CreateNamedPipe(pipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_WRITE_THROUGH,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
			PIPE_UNLIMITED_INSTANCES, 1024, 0, 0, nullptr);
		assert(hPipe != INVALID_HANDLE_VALUE);

		// start the client with the necessary info
		SHELLEXECUTEINFOA sei = { sizeof(sei) };
		sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_WAITFORINPUTIDLE;
		sei.lpVerb = "open";
		sei.lpFile = "SharedTextureTestClient.exe";
		sei.lpDirectory = "E:\\gitrepos\\SharedTextureTest\\SharedTextureTestClient\\bin\\Debug\\net10.0-windows";
		ShellExecuteExA(&sei);

		// get D3D11 shared handle to the Vulkan image 
		auto hD3D11TextureMem = (HANDLE)device.getMemoryWin32HandleKHR({
			.memory = vulkanImageMemory,
			.handleType = vk::ExternalMemoryHandleTypeFlagBits::eD3D11Texture
			});
		
		HANDLE hTargetTextureMem;
		assert(sei.hProcess && sei.hProcess != INVALID_HANDLE_VALUE);
		DuplicateHandle(GetCurrentProcess(), hTextureMem, sei.hProcess, &hTargetTextureMem, 0, TRUE, DUPLICATE_SAME_ACCESS);

		auto message = format("{} {} {}\n", (size_t)hTargetTextureMem, texWidth, texHeight);

		ConnectNamedPipe(hPipe, nullptr);

		DWORD bytesWritten;
		WriteFile(hPipe, message.c_str(), (DWORD)message.size(), &bytesWritten, nullptr);
		assert(bytesWritten == message.size());

		// wait for confirmation
		char buffer[32]{};
		DWORD bytesRead{};
		ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, nullptr);

		CloseHandle(hPipe);
	}

	// trigger new frames on a timer
	constexpr double requestedFrameTime = 1.0 / 30.0;
	auto renderEvent = SDL_RegisterEvents(1);
	SDL_AddTimerNS(Uint64(requestedFrameTime * SDL_NS_PER_SECOND), [](auto data, auto timer, auto interval) {
		SDL_Event event{ .type = (Uint32)(size_t)data };
		SDL_PushEvent(&event);
		return Uint64(requestedFrameTime * SDL_NS_PER_SECOND);
		}, (void*)(size_t)renderEvent);

#ifdef _DEBUG
	// framerate counter
	Uint64 frameCount = 0;
	Uint64 lastTime = SDL_GetTicksNS();
#endif

	// main loop
	while (true)
	{
		SDL_Event event;
		if (SDL_WaitEvent(&event))
			if (event.type == renderEvent)
			{
				// dispatch compute shader to the shared texture
				glUseProgram(computeProgram);
				glBindImageTexture(0, glTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
				glUniform1f(glGetUniformLocation(computeProgram, "time"), (float)SDL_GetTicksNS() / SDL_NS_PER_SECOND);
				glDispatchCompute((GLuint)ceil(texWidth / 16.0f), (GLuint)ceil(texHeight / 16.0f), 1);
				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

				// render
				glClear(GL_COLOR_BUFFER_BIT);

				glUseProgram(shaderProgram);
				glBindVertexArray(vao);
				glBindTexture(GL_TEXTURE_2D, glTexture);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				SDL_GL_SwapWindow(window);

#ifdef _DEBUG
				// framerate counter
				frameCount++;
				auto currentTime = SDL_GetTicksNS();
				if (currentTime - lastTime >= SDL_NS_PER_SECOND) {
					SDL_SetWindowTitle(window, format("Server | FPS: {}", frameCount).c_str());
					frameCount = 0;
					lastTime = currentTime;
				}
#endif

			}
			else if (event.type == SDL_EVENT_QUIT)
				break;
	}
}
