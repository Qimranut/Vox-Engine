/*************************************************************************************************************
\file       world_pass.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      WorldPass, the GL-free interface graphics::System calls to draw the world after the entity
            pass; the voxel subsystem implements it.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace graphics::camera { class Camera; }

namespace graphics::renderer
{
	/**
	 * A renderer for the world itself, drawn by graphics::System::render()
	 * after the entity pass and before the debug lines. GL-free interface
	 * so graphics never learns what the world is made of; the voxel
	 * subsystem implements it.
	 *
	 */
	class WorldPass
	{
	public:

		virtual ~WorldPass() = default;

		/*!
			@brief
				Draws into the currently bound framebuffer and viewport. GL thread.
			@param camera
				Camera to draw from
			@param viewportWidth
				Viewport width in pixels, for screen-space size decisions
			@param viewportHeight
				Viewport height in pixels, for screen-space size decisions
			*/
		virtual void render(const camera::Camera& camera, int viewportWidth, int viewportHeight) = 0;
	};
}
