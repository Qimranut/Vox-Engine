#pragma once

// GLM
#include <glm/glm.hpp>


namespace cpn
{
	struct Visibility
	{
		bool visible_			{ false };

		// sRGB colour multiplied with the albedo texture (white when there is
		// none), so the same cube reads as ground, prop or player at a glance.
		// The renderer converts it to linear before lighting.
		glm::vec3 tint_			{ 1.f };
	};
}
