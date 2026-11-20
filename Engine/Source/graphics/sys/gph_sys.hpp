/*************************************************************************************************************
\file       gph_sys.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      graphics::System: owns the GL resources, textures, entity renderer and the camera with its fly
            and orbit controllers; moves the camera in update() and draws the scene in render().

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// System interface
#include "../../scheduler/scheduler.hpp"

// Renderer
#include "../renderer/renderer.hpp"
#include "../renderer/world_pass.hpp"
#include "../model/model.hpp"
#include "../shaders/shader.hpp"
#include "../camera/camera.hpp"
#include "../camera/controller/fly_controller.hpp"
#include "../camera/controller/orbit_controller.hpp"
#include "../texture/texture_types.hpp"
#include "../skybox/skybox.hpp"
#include "../ibl/ibl.hpp"

// Deliberately NOT including the input system. Graphics consumes camera
// intent as a plain struct and knows nothing about keys, pads or GLFW - see
// controller_input.hpp. Cutting the edge also keeps graphics free of the
// main-thread-only GLFW input calls, which matters the day the graphics
// worker is handed to the scheduler.

// stds
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace vox
{
	class ResourceManager;
}

namespace graphics::texture
{
	class TextureManager;
	class SamplerCache;
}

namespace graphics
{
    class TextRenderer;
	/**
	 * Which controller drives the camera.
	 *
	 */
	enum class CameraMode
	{
		Fly,		// free-flying debug / editor camera
		Orbit		// third-person over-the-shoulder
	};

	class System : public scheduler::ISystem
	{
	public:
		/*!
			@brief
				Stores the window and scene; touches no GL until init().
			@param window
				Window system whose context is drawn to
			@param ecs
				Scene to draw. Read only - graphics never changes entities.
			*/
		System(window::System& window, const ecs::ECSManager& ecs);

		/*!
			@brief
				Frees every GL object. Must run while the context still exists;
				a System that was never init()ed touches no GL at all.
			*/
		~System() override;

		/*!
			@brief
				Creates every GL resource. Needs the window's context current.

				Throws std::runtime_error if the context is not OpenGL 4.5, which
				the texture code's direct state access requires.
			@param resources
				Decodes texture files. Must outlive this system.
			*/
		void init(vox::ResourceManager& resources);

		/*!
			@brief
				Moves the camera with the active controller. Call once per frame.
			@param dt
				Real frame time in seconds, not the fixed simulation step
			*/
		void update(double dt) override;

		/*!
			@brief
				Draws the scene into whatever framebuffer and viewport are bound.
				Does not present - the engine swaps buffers once per frame.
			*/
		void render();

		/*!
			@brief
				Name of this system for the scheduler.
			@return
				"Graphics"
			*/
		const char* getName() const override;

		/*!
			@brief
				The camera the renderer draws from.
			@return
				Mutable reference, so callers can reposition or reconfigure it
			*/
		camera::Camera& camera() { return camera_; }

		/*!
			@brief
				The camera the renderer draws from, read only.
			@return
				Const reference to the camera
			*/
		const camera::Camera& camera() const { return camera_; }

		/*!
			@brief
				The free-fly camera controller.
			@return
				Mutable reference to the fly controller
			*/
		camera::FlyController& fly() { return fly_; }

		/*!
			@brief
				The third-person orbit camera controller.
			@return
				Mutable reference to the orbit controller
			*/
		camera::OrbitController& orbit() { return orbit_; }

		/*!
			@brief
				Selects which controller drives the camera.
			@param mode
				Fly or Orbit
			*/
		void setCameraMode(CameraMode mode) { mode_ = mode; }

		/*!
			@brief
				Which controller drives the camera.
			@return
				The current camera mode
			*/
		CameraMode cameraMode() const { return mode_; }

		/*!
			@brief
				Sets the camera mode by name, for the scripting API.
			@param mode
				"fly" or "orbit"; anything else is ignored
			*/
		void setCameraMode(const std::string& mode);
        void setScene2D(bool enabled);
        bool scene2D() const { return scene2D_; }

		/*!
			@brief
				The camera intent the next update() consumes.

				Written by the engine's input event subscribers, which are the
				only layer that knows both the input system's events and this
				struct. Graphics never resolves a keybinding itself.
			@return
				Mutable intent for the active controller
			*/
		camera::ControllerInput& cameraInput() { return cameraInput_; }

		/*!
			@brief
				Sets the world renderer drawn after the entity pass.

				Not owned; must be cleared before it is destroyed.
			@param pass
				World renderer, or nullptr for none
			*/
		void setWorldPass(renderer::WorldPass* pass) { worldPass_ = pass; }

		/*!
			@brief
				Every GPU texture. Valid after init().
			@return
				The texture manager, for loading, reloading and generated textures
			*/
		texture::TextureManager& textures() { return *textures_; }

		/*!
			@brief
				Frees textures no RenderMaterial references any more. render() also
				does this on its own every few seconds.
			@return
				Number of textures freed
			*/
		std::size_t collectUnusedTextures();

		skybox::Skybox& skybox() { return *skybox_; }

	private:

		// Declared before the renderer so they are destroyed after it.
		std::unique_ptr<texture::SamplerCache> samplers_;
		std::unique_ptr<texture::TextureManager> textures_;
		std::vector<texture::TextureHandle> texturesInUse_;
		unsigned framesSinceTextureCollect_ = 0;
		bool initialized_ = false;

		std::unique_ptr<renderer::Renderer> renderer_;
        std::unique_ptr<TextRenderer> textRenderer_;
		std::unique_ptr<skybox::Skybox> skybox_;
		std::unique_ptr<ibl::IBL> ibl_;
		window::System& window_;
		const ecs::ECSManager& ecs_;
		renderer::WorldPass* worldPass_ = nullptr;
		int viewportWidth_ = 0, viewportHeight_ = 0;
		std::unique_ptr<model::ModelManager> modelManager_;
		GLuint shaderProgram_ = 0;

		camera::Camera camera_;
        camera::Camera saved3DCamera_, saved2DCamera_;
        bool scene2D_{}, has2DCamera_{};
		camera::FlyController fly_;
		camera::OrbitController orbit_;
		CameraMode mode_ = CameraMode::Fly;

		camera::ControllerInput cameraInput_{};

		/*!
			@brief
				Sets the camera's aspect ratio from the currently bound GL viewport.

				Read rather than driven by a resize event, because the target
				changes per call: the window in Game, the viewport panel's
				framebuffer in the Editor. It also keeps GL calls on the thread
				holding the context by construction.
			*/
		void syncAspectToViewport();
	};
}
