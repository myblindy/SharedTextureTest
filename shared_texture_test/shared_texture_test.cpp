#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <ranges>
#include <sstream>
#include <regex>
#include <assert.h>

#include "glad/glad.h"

#include <SDL3/SDL.h>

#include <windows.h>

using namespace std;
namespace views = std::ranges::views;

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

#ifdef _DEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

	glDebugMessageCallback([](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
		if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

		std::cout << "---------------" << std::endl;
		std::cout << "Debug message (" << id << "): " << message << std::endl;

		switch (source)
		{
		case GL_DEBUG_SOURCE_API:             std::cout << "Source: API"; break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   std::cout << "Source: Window System"; break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER: std::cout << "Source: Shader Compiler"; break;
		case GL_DEBUG_SOURCE_THIRD_PARTY:     std::cout << "Source: Third Party"; break;
		case GL_DEBUG_SOURCE_APPLICATION:     std::cout << "Source: Application"; break;
		case GL_DEBUG_SOURCE_OTHER:           std::cout << "Source: Other"; break;
		} std::cout << std::endl;

		switch (type)
		{
		case GL_DEBUG_TYPE_ERROR:               std::cout << "Type: Error"; break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: std::cout << "Type: Deprecated Behaviour"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  std::cout << "Type: Undefined Behaviour"; break;
		case GL_DEBUG_TYPE_PORTABILITY:         std::cout << "Type: Portability"; break;
		case GL_DEBUG_TYPE_PERFORMANCE:         std::cout << "Type: Performance"; break;
		case GL_DEBUG_TYPE_MARKER:              std::cout << "Type: Marker"; break;
		case GL_DEBUG_TYPE_PUSH_GROUP:          std::cout << "Type: Push Group"; break;
		case GL_DEBUG_TYPE_POP_GROUP:           std::cout << "Type: Pop Group"; break;
		case GL_DEBUG_TYPE_OTHER:               std::cout << "Type: Other"; break;
		} std::cout << std::endl;

		switch (severity)
		{
		case GL_DEBUG_SEVERITY_HIGH:         std::cout << "Severity: high"; break;
		case GL_DEBUG_SEVERITY_MEDIUM:       std::cout << "Severity: medium"; break;
		case GL_DEBUG_SEVERITY_LOW:          std::cout << "Severity: low"; break;
		case GL_DEBUG_SEVERITY_NOTIFICATION: std::cout << "Severity: notification"; break;
		} std::cout << std::endl;
		std::cout << std::endl;
		}, nullptr);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
#endif

	GLuint sharedTexture;
	GLuint glTextureMemory;
	int texWidth, texHeight;
	HANDLE frameReadyEvent;
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
		sei.lpDirectory = "E:\\gitrepos\\SharedTextureTest\\SharedTextureTestClient\\bin\\x64\\Debug\\net10.0-windows";
		ShellExecuteExA(&sei);

		ConnectNamedPipe(hPipe, nullptr);

		// read the shared texture information from the client
		DWORD bytesRead;
		char buffer[128]{};
		do {
			ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
		} while (bytesRead == 0);

		{
			regex re(R"(^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\w+)\s*$)");
			smatch match;
			string input = { buffer, bytesRead };
			if (!regex_match(input, match, re) || match.size() != 5) {
				cerr << "Error: Invalid data received from client: " << input << endl;
				abort();
			}


			// get local handle from the client shared texture handle
			auto hClientSharedTexture = (HANDLE)_strtoi64(match[1].str().c_str(), nullptr, 10);
			HANDLE hTextureMem;
			// OpenProcess(PROCESS_DUP_HANDLE, false, processAPID); ?
			DuplicateHandle(sei.hProcess, hClientSharedTexture, GetCurrentProcess(), &hTextureMem, 0, false, DUPLICATE_SAME_ACCESS);
			// close process handle

			// create the texture from the shared handle
			glCreateTextures(GL_TEXTURE_2D, 1, &sharedTexture);

			glTextureParameteri(sharedTexture, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT); // D3D11 side is D3D11_TEXTURE_LAYOUT_UNDEFINED
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			glCreateMemoryObjectsEXT(1, &glTextureMemory);

			texWidth = atoi(match[2].str().c_str());
			texHeight = atoi(match[3].str().c_str());
			glImportMemoryWin32HandleEXT(glTextureMemory, texWidth * texHeight * 4 * 2, GL_HANDLE_TYPE_D3D11_IMAGE_EXT, hTextureMem);

			if (glAcquireKeyedMutexWin32EXT(glTextureMemory, 0, INFINITE))
			{
				glTextureStorageMem2DEXT(sharedTexture, 1, GL_RGBA8, texWidth, texHeight, glTextureMemory, 0);
				glReleaseKeyedMutexWin32EXT(glTextureMemory, 0);
			}
			else
			{
				cerr << "Error: glAcquireKeyedMutexWin32EXT failed" << endl;
				abort();
			}

			assert(glGetError() == GL_NO_ERROR);

			frameReadyEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, ("Global\\" + match[4].str()).c_str());
			assert(frameReadyEvent);

			// write the end
			DWORD bytesWritten;
			WriteFile(hPipe, "OK", 3, &bytesWritten, nullptr);
		}

		CloseHandle(hPipe);
	}

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
	//glWaitSemaphoreEXT(glImageTransitionCompleteSemaphore, 0, nullptr, 0, nullptr, nullptr);

	assert(glGetError() == GL_NO_ERROR);

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
				glAcquireKeyedMutexWin32EXT(glTextureMemory, 0, INFINITE);

				// dispatch compute shader to the shared texture
				glUseProgram(computeProgram);
				glBindImageTexture(0, sharedTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
				glUniform1f(glGetUniformLocation(computeProgram, "time"), (float)SDL_GetTicksNS() / SDL_NS_PER_SECOND);
				glDispatchCompute((GLuint)ceil(texWidth / 16.0f), (GLuint)ceil(texHeight / 16.0f), 1);
				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
				SetEvent(frameReadyEvent);

				// render
				glClear(GL_COLOR_BUFFER_BIT);

				glUseProgram(shaderProgram);
				glBindVertexArray(vao);
				glBindTexture(GL_TEXTURE_2D, sharedTexture);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				glReleaseKeyedMutexWin32EXT(glTextureMemory, 0);

				SDL_GL_SwapWindow(window);

#ifdef _DEBUG
				// framerate counter
				frameCount++;
				auto currentTime = SDL_GetTicksNS();
				if (currentTime - lastTime >= SDL_NS_PER_SECOND) {
					//SDL_SetWindowTitle(window, format("Server | FPS: {}", frameCount).c_str());
					frameCount = 0;
					lastTime = currentTime;
				}
#endif

			}
			else if (event.type == SDL_EVENT_QUIT)
				break;
	}
}
