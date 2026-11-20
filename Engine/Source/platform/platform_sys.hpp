#pragma once

// Config
#include "../config/config.hpp"

// Glew
#include <GL/glew.h>

// GLFW
#include <GLFW/glfw3.h>

// stds
#include <stdexcept>
#include <filesystem>

#include <iostream>


namespace platform
{
	class System
	{
	public:

		System()
		{
			// GLFW
#if VOX_GLFW_ACTIVE
			if (glfwInit() == GLFW_FALSE)
				throw std::runtime_error("GLFW enabled and GLFW Initialization failed.");
			else
				glfwInit_ = true;
#endif 
		}

		~System()
		{
#if VOX_GLFW_ACTIVE
			// GLFW
			glfwTerminate();
#endif 
		}

	private:

		bool glfwInit_{};
		bool freeTypeInit{};
	};
}
