/*************************************************************************************************************
\file       line_renderer.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      LineRenderer: sets up the position/colour vertex layout and draws a batch of world-space
            debug lines from one dynamic buffer re-uploaded each draw.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "line_renderer.hpp"

// GLM
#include <glm/gtc/type_ptr.hpp>

// stds
#include <cstddef>
#include <iostream>


namespace graphics::renderer
{
	/*!
		@brief
			Looks up u_viewProj and builds the VAO/VBO with the DebugVertex layout.
		@param programID
			Program built from DebugLine.vert / DebugLine.frag
		*/
	LineRenderer::LineRenderer(GLuint programID)
		:
		program_{ programID },
		buffer_{ std::make_unique<Buffer>() }
	{
		if (program_ != 0)
			locViewProj_ = glGetUniformLocation(program_, "u_viewProj");

		if (locViewProj_ < 0)
			std::cerr << "[LineRenderer] shader has no u_viewProj uniform\n";

		const GLsizei stride = static_cast<GLsizei>(sizeof(DebugVertex));

		buffer_->BindVAO();
		buffer_->BindVBO();
		buffer_->SetData(0, nullptr, GL_DYNAMIC_DRAW);

		buffer_->SetAttribute(0, 3, GL_FLOAT, GL_FALSE, stride,
			reinterpret_cast<const void*>(offsetof(DebugVertex, pos_)));

		buffer_->SetAttribute(1, 3, GL_FLOAT, GL_FALSE, stride,
			reinterpret_cast<const void*>(offsetof(DebugVertex, color_)));

		buffer_->UnbindVAO();
		buffer_->UnbindVBO();
	}

	/*!
		@brief
			Uploads the vertices and draws them as GL_LINES; does nothing with no program or fewer than two vertices.
		@param vertices
			World-space line endpoints, two per line
		@param viewProj
			Camera view-projection
		*/
	void LineRenderer::draw(const std::vector<DebugVertex>& vertices, const glm::mat4& viewProj)
	{
		if (program_ == 0 || vertices.size() < 2)
			return;

		glUseProgram(program_);

		if (locViewProj_ >= 0)
			glUniformMatrix4fv(locViewProj_, 1, GL_FALSE, glm::value_ptr(viewProj));

		buffer_->BindVAO();
		buffer_->BindVBO();

		// Full re-specification each frame lets the driver orphan the old
		// storage instead of stalling on a buffer the GPU may still be reading.
		buffer_->SetData(
			static_cast<GLsizeiptr>(vertices.size() * sizeof(DebugVertex)),
			vertices.data(),
			GL_DYNAMIC_DRAW);

		glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));

		buffer_->UnbindVAO();
		buffer_->UnbindVBO();

		glUseProgram(0);
	}
}
