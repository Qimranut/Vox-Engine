/*************************************************************************************************************
\file       lod_traversal.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Helpers for the voxel renderer's LoD traversal: projected screen area of a box, the split
            threshold, and point-in-box.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>

// stds
#include <cmath>
#include <limits>


namespace voxel::render
{
	/*!
		@brief
			Projected screen area of an axis-aligned box, in normalised viewport units (1 = the whole viewport).

			Computed on the CPU: the sum of the three visible faces seen from
			each of two opposite corners, halved.
		@param viewProj
			View-projection matrix
		@param mn
			Box minimum corner
		@param mx
			Box maximum corner
		@return
			The area; +infinity when any corner is behind the near plane,
			which forces a descent - the camera is inside or right beside the box
		*/
	inline float projectedArea(const glm::mat4& viewProj, const glm::vec3& mn, const glm::vec3& mx)
	{
		glm::vec2 p[8];
		for (int i = 0; i < 8; ++i)
		{
			const glm::vec3 corner{ (i & 4) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 1) ? mx.z : mn.z };
			const glm::vec4 clip = viewProj * glm::vec4(corner, 1.f);
			if (clip.w <= 1e-4f)
				return std::numeric_limits<float>::infinity();
			p[i] = glm::vec2(clip) / clip.w * 0.5f + 0.5f;
		}

		const auto crossMag = [](const glm::vec2& a, const glm::vec2& b) { return std::fabs(a.x * b.y - b.x * a.y); };

		// Corner index bits: x = 4, y = 2, z = 1.
		const glm::vec2 a = p[4] - p[0], b = p[2] - p[0], c = p[1] - p[0];
		const glm::vec2 d = p[3] - p[7], e = p[5] - p[7], f = p[6] - p[7];

		return 0.5f * (crossMag(a, b) + crossMag(a, c) + crossMag(c, b)
					 + crossMag(d, e) + crossMag(d, f) + crossMag(f, e));
	}

	/*!
		@brief
			Split threshold: a node projecting larger than subDivisionSize pixels squared is refined.
		@param subDivisionSize
			Node size on screen, in pixels, above which to descend
		@param viewportW
			Viewport width in pixels
		@param viewportH
			Viewport height in pixels
		@return
			The threshold in normalised viewport area; 0 for an empty viewport
		*/
	inline float descendThreshold(float subDivisionSize, int viewportW, int viewportH)
	{
		const float pixels = static_cast<float>(viewportW) * static_cast<float>(viewportH);
		return pixels > 0.f ? (subDivisionSize * subDivisionSize) / pixels : 0.f;
	}

	/*!
		@brief
			Tests whether a point lies inside an axis-aligned box, faces included.
		@param p
			Point
		@param mn
			Box minimum corner
		@param mx
			Box maximum corner
		@return
			true when inside
		*/
	inline bool pointInside(const glm::vec3& p, const glm::vec3& mn, const glm::vec3& mx)
	{
		return p.x >= mn.x && p.y >= mn.y && p.z >= mn.z && p.x <= mx.x && p.y <= mx.y && p.z <= mx.z;
	}
}
