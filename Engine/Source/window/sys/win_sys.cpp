#include "win_sys.hpp"

namespace window
{
	System::System(::events::Bus& bus)
		:
		bus_{ bus },

		focused_window_		{ nullptr }
	{}

	bool System::createWindow()
	{
		// Context hints. Without these GLFW picks a compatibility context and, more
		// importantly, makes no promise about a depth buffer - which silently turns
		// GL_DEPTH_TEST and the GL_DEPTH_BUFFER_BIT clear into no-ops.
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,	4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,	5);
		glfwWindowHint(GLFW_OPENGL_PROFILE,			GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_DEPTH_BITS,				24);

#ifndef NDEBUG
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT,	GLFW_TRUE);
#endif

		// Window creation
		GLFWwindow* window = glfwCreateWindow(
			cfg::set.windowDim.w_, cfg::set.windowDim.h_, "Main Window", nullptr, nullptr);

		if (!window)
		{
			std::cerr << "Failed to create GLFW window." << std::endl;
			glfwTerminate();

			return false;
		}

		glfwMakeContextCurrent(window);

		// Vsync
		glfwSwapInterval(1);

		// GLEW reports spurious errors on core contexts without this
		glewExperimental = GL_TRUE;

		GLenum result = glewInit();

		if (result != GLEW_OK)
		{
			std::cout << "GLEW failed: "
				<< glewGetErrorString(result)
				<< '\n';
			glfwDestroyWindow(window);
			return false;
		}
		else
		{
			std::cout << "GLEW initialized!\n";
			std::cout << "OpenGL: "
				<< glGetString(GL_VERSION)
				<< '\n';
		}

		// Set window pos
		glfwSetWindowPos(window, cfg::set.windowDim.x_, cfg::set.windowDim.y_);

		// Set focused window.
		// This is unconditional on purpose: the context was just made current on this
		// window, so it is the render target regardless of whether the OS has given it
		// keyboard focus yet. Leaving it null when GLFW_FOCUSED is false meant
		// publishWindowCreated() handed subscribers a null GLFWwindow*, and every GLFW
		// callback registration against it failed silently.
		focused_window_ = window;

		windows_.push_back(window);

		publishWindowCreated(window);

		return true;
	}

	void System::shutdown()
	{
		// Tell subscribers first, while the pointer is merely stale rather
		// than dangling. The input system holds one to drive the cursor mode,
		// and calling glfwSetInputMode on a destroyed window is undefined.
		for (const auto& w : windows_)
		{
			bus_.publish<events::WindowDestroyed>(events::WindowDestroyed{ w });
		}

		// Detach glfw callbacks
		for (const auto& w : windows_)
		{
			glfwSetKeyCallback(w, nullptr);
			glfwSetCharCallback(w, nullptr);
			glfwSetWindowFocusCallback(w, nullptr);
			glfwSetCursorPosCallback(w, nullptr);
			glfwSetMouseButtonCallback(w, nullptr);
			glfwSetScrollCallback(w, nullptr);
			glfwSetWindowUserPointer(w, nullptr);
		}

		// Flush any remaining events
		glfwPollEvents();

		// Destroy windows
		for (const auto& win : windows_)
		{
			glfwDestroyWindow(win);
		}

		windows_.clear();
		focused_window_ = nullptr;
	}

	glm::vec2 System::cursorPos()
	{
		if (!focused_window_)
			return { 0.f, 0.f };

		double x{}, y{};
		glfwGetCursorPos(focused_window_, &x, &y);
		return { static_cast<float>(x), static_cast<float>(y) };
	}

	glm::vec2 System::cursorPosNorm()
	{
		if (!focused_window_)
			return { 0.f, 0.f };

		double x{}, y{};
		int w{}, h{};

		glfwGetCursorPos(focused_window_, &x, &y);
		glfwGetFramebufferSize(focused_window_, &w, &h);

		// Minimized -> zero framebuffer, don't divide by it
		if (w <= 0 || h <= 0)
			return { 0.f, 0.f };

		return { static_cast<float>(x) / w, static_cast<float>(y) / h };
	}

	glm::vec2 System::focusedWinDim()
	{
		if (!focused_window_)
			return { 0.f, 0.f };

		int w{}, h{};
		glfwGetFramebufferSize(focused_window_, &w, &h);
		return { static_cast<float>(w), static_cast<float>(h) };
	}

	std::vector<GLFWwindow*> System::windows()
	{
		return windows_;
	}

	GLFWwindow* System::focused_window()
	{
		return focused_window_;
	}

	void System::publishWindowCreated(GLFWwindow* window)
	{
		bus_.publish<events::WindowCreated>(events::WindowCreated{ window });
	}
}
