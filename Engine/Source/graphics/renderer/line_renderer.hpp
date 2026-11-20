/*************************************************************************************************************
\file       line_renderer.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      LineRenderer, which draws batches of world-space coloured debug lines (the voxel section
            bounds) on the GL thread.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Renderer
#include "renderer.hpp"
#include "debug_vertex.hpp"

// GLEW
#include <GL/glew.h>

// GLM
#include <glm/glm.hpp>

// stds
#include <memory>
#include <vector>


namespace graphics::renderer
{
	/**
	 * Draws batches of world-space coloured lines - the voxel section bounds.
	 *
	 * One dynamic vertex buffer, re-uploaded each frame it is used. GL thread
	 * only.
	 *
	 */
	class LineRenderer
	{
	public:

		/*!
			@brief
				Creates the line renderer over a shader program.
			@param programID
				Program built from DebugLine.vert / DebugLine.frag
			*/
		explicit LineRenderer(GLuint programID);

		/*!
			@brief
				Draws GL_LINES, two vertices per line, depth tested.
			@param vertices
				World-space line endpoints
			@param viewProj
				Camera view-projection
			*/
		void draw(const std::vector<DebugVertex>& vertices, const glm::mat4& viewProj);

	private:

		GLuint					program_		{ 0 };
		GLint					locViewProj_	{ -1 };
		std::unique_ptr<Buffer>	buffer_;
	};
}
