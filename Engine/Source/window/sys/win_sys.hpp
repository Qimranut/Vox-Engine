#pragma once

// Window events
#include "../events/win_events.hpp"

// Event Bus
#include "../../events/bus.hpp"

// Config
#include "../../config/config.hpp"

// Glew
#include <GL/glew.h>

// GLFW
#include <GLFW/glfw3.h>

// GLM
#include <glm/glm.hpp>

// stds
#include <iostream>
#include <vector>
#include <concepts>


namespace window
{
	class System
	{
	public:

		System(::events::Bus&);

		/**
		 * Cleans up windows and detaches callbacks
		 *
		 */
		void shutdown();

		// Accessors

		std::vector<GLFWwindow*> windows();

		GLFWwindow* focused_window();

		// Utility

		/**
		 * Creates a window and makes it the focused window.
		 *
		 * [return] Success code
		 * true: Window creation successful
		 * false: Window creation unsuccessful
		 *
		 */
		bool createWindow();

		/**
		 * Return cursor pos relative to focused window pos
		 *
		 * [return]
		 * Cursor position
		 *
		 */
		glm::vec2 cursorPos();

		/**
		 * Returns normalized cursor pos relative to focused window pos
		 *
		 * [return]
		 * Normalized cursor position
		 *
		 */
		glm::vec2 cursorPosNorm();

		/**
		 * Returns focused window's dimensions
		 *
		 * [return]
		 * Focused window's dimensions
		 *
		 */
		glm::vec2 focusedWinDim();

	private:

		std::vector<GLFWwindow*> windows_;

		GLFWwindow* focused_window_;

		::events::Bus& bus_;

		/**
		 * Window event, fires when a new window is made.
		 *
		 * [param] window
		 * The newly created window
		 *
		 */
		void publishWindowCreated(GLFWwindow* window);
	};
}
