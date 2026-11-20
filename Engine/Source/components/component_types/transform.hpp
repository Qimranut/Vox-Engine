#pragma once

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>


namespace cpn
{
	struct Transform
	{
		glm::vec3 pos_			{ 0.f };

		// Quaternion rather than Euler: physics writes rotations back every
		// frame, and round-tripping arbitrary tumbling bodies through Euler
		// angles loses information near gimbal lock. glm::quat is (w, x, y, z).
		glm::quat rot_			{ 1.f, 0.f, 0.f, 0.f };

		glm::vec3 scale_		{ 1.f };
	};
}
