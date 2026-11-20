/*************************************************************************************************************
\file       normal_decode.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      CPU mirror of the shader's BC5 normal decode (rebuilds Z from X and Y), used by the
            texture self-check so it tests the same maths the GPU runs.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>

// stds
#include <algorithm>
#include <cmath>


namespace graphics::texture
{
	/*!
		@brief
			Rebuilds a tangent-space normal from a BC5 texel.

			BC5 keeps only X and Y (red and green, 8bpp), which gives each of them
			the precision a full RGB block would split three ways. Z is recomputed
			because the normal is unit length and points out of the surface:
			X^2 + Y^2 + Z^2 = 1, so Z = sqrt(1 - X^2 - Y^2).

			Mirrors decodeNormalBC5() in ColorFragmentShader1.frag, so the texture
			self-check exercises the same maths the GPU runs. Change both together.
		@param r
			Sampled red, [0, 1]
		@param g
			Sampled green, [0, 1]
		@return
			Unit normal, +Z out of the surface, OpenGL (+Y up) green convention
		*/
	inline glm::vec3 decodeNormalBC5(float r, float g)
	{
		const glm::vec2 xy = glm::vec2(r, g) * 2.f - 1.f;

		// Filtering and block error can push X^2 + Y^2 just past 1.
		const float z = std::sqrt(std::max(0.f, 1.f - glm::dot(xy, xy)));

		return glm::normalize(glm::vec3(xy, z));
	}
}
