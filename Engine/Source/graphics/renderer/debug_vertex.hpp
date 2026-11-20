/*************************************************************************************************************
\file       debug_vertex.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      DebugVertex, one coloured end of a debug line, shared by line producers and the line renderer.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>


namespace graphics::renderer
{
	/**
	 * One end of a debug line.
	 *
	 * glm only, so physics can produce these without touching OpenGL and the
	 * renderer can draw them without touching Jolt. Standard layout: the line
	 * renderer uploads a vector of these as-is.
	 *
	 */
	struct DebugVertex
	{
		glm::vec3	pos_	{ 0.f };
		glm::vec3	color_	{ 1.f };
	};
}
