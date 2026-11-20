#pragma once

struct GLFWwindow;

namespace window::events
{
	struct WindowCreated
	{
		GLFWwindow* newWindow{ nullptr };
	};

	/**
	 * A window is about to be destroyed.
	 *
	 * Published BEFORE glfwDestroyWindow, so a subscriber still holding the
	 * pointer can drop it while it is merely stale rather than dangling.
	 *
	 */
	struct WindowDestroyed
	{
		GLFWwindow* oldWindow{ nullptr };
	};
}
