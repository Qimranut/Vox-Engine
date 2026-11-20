/*************************************************************************************************************
\file       engine.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The ENTIRE public surface of Engine: VoxEngine and the optional ApplicationLayer, with
            forward declarations only. Subsystem headers drag glm, EnTT, GLFW, Jolt, sol2 and FMOD
            into every translation unit that includes them, so they live in Engine/Source
            (engine/systems/system_manager.cpp and friends).

            This is what lets Engine/CMakeLists.txt keep every dependency PRIVATE. If a third-party
            type ever appears below, that dependency has to become PUBLIC and Editor + Game start
            recompiling whenever it changes. std::unique_ptr<T> is legal with an INCOMPLETE T as
            long as the destructor is defined out of line where T is complete - which is why
            ~VoxEngine() and VoxEngine() are declared here and defined in engine.cpp.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <memory>
#include <filesystem>
#include <functional>
#include <array>


// Types the application layer is handed, forward declared
namespace events	{ class Bus; }
namespace ecs		{ class ECSManager; }
namespace voxel		{ class System; }
namespace graphics	{ class System; }
struct GLFWwindow;
namespace vox { class SceneManager; class ResourceManager; }

namespace scripting::lua
{
	class Interface;
}


namespace engine
{
	// Camera matrices for presentation overlays, copied after rendering so
	// projection matches the caller's framebuffer. Column-major, OpenGL depth.
	struct SceneView
	{
		std::array<float, 16> view{}, projection{};
		bool orthographic{};
	};
	// Optional presentation layer; Engine does not depend on ImGui or Editor.
	class ApplicationLayer
	{
	public:
		virtual ~ApplicationLayer() = default;

		/*!
			@brief
				Hands the layer everything it may use, once the engine is up.
			@param GLFWwindow*
				The main window.
			@param ecs::ECSManager&
				The ECS.
			@param scripting::lua::Interface&
				The Lua interface.
			@param vox::SceneManager&
				The scene manager.
			@param vox::ResourceManager&
				The resource manager.
			@param events::Bus&
				The event bus.
			@param voxel::System&
				The voxel terrain system.
			@param graphics::System&
				The graphics system (for the camera).
			*/
		virtual void onAttach(GLFWwindow*, ecs::ECSManager&, scripting::lua::Interface&,
			vox::SceneManager&, vox::ResourceManager&, events::Bus&, voxel::System&, graphics::System&) = 0;

		/*!	
			@brief
				Draws the layer's frame; the layer decides when the scene is rendered.
			@param renderScene
				Renders the scene into whatever viewport is bound.
			*/
		virtual void onFrame(const std::function<SceneView()>& renderScene) = 0;

		/*!
			@brief
				Releases what the layer holds, before the engine shuts down.
			*/
		virtual void onDetach() noexcept = 0;

		/*!
			@brief
				Whether the simulation should run this frame.
			@return
				True while playing.
			*/
		virtual bool isPlaying() const = 0;
        virtual bool wantsCameraInput() const { return true; }

		/*!
			@brief
				Whether the layer's UI owns the keyboard this frame.

				Asked once per frame, right before input is resolved. While true, that device drives
				no action and publishes no press events, so typing into the layer's UI never reaches
				the camera or scripts.
			@return
				False unless overridden.
			*/
		virtual bool capturesKeyboard() const { return false; }

		/*!
			@brief
				Whether the layer's UI owns the mouse this frame; same rules as capturesKeyboard().
			@return
				False unless overridden.
			*/
		virtual bool capturesMouse() const { return false; }

		/*!
			@brief
				The least time in seconds between the starts of two frames.

				Asked once per frame, before the OS events are read. The wait ends early on any OS
				event, so a long interval (an idle editor) still answers the mouse at once.
			@return
				Seconds between frame starts; zero (the default) leaves the frame rate to vsync.
			*/
		virtual double frameInterval() const { return 0.0; }
	};

	class SystemManager;
	class FrameLoop;

	/**
	 * The engine: init once, update once per frame, shut down once.
	 *
	 *     engine.init(layer);
	 *     while (engine.update()) {}
	 *     engine.shutdown();
	 *
	 * Subsystems are owned by SystemManager, their bus links live in
	 * engine/wiring, and the per-frame order is FrameLoop's.
	 *
	 */
	class VoxEngine
	{
	public:

		/*!
			@brief
				Creates an engine with nothing running; call init() to bring it up.
			*/
		VoxEngine();

		/*!
			@brief
				Shuts the engine down if it is still running.
			*/
		~VoxEngine();

		// The engine owns OS handles and worker threads - copying it is never
		// what anyone means.
		VoxEngine(const VoxEngine&)				= delete;
		VoxEngine& operator=(const VoxEngine&)	= delete;

		/*!
			@brief
				Brings up every subsystem, runs the startup checks and scripts, and attaches the layer.
			@param layer
				Optional presentation layer; must outlive the engine.
			@param assetRoot
				Root assets load relative to.
			@param initialScene
				Scene to load; empty for none.
			*/
		void init(ApplicationLayer* layer = nullptr,
			const std::filesystem::path& assetRoot = "Assets",
			const std::filesystem::path& initialScene = {});

		/*!
			@brief
				Runs one frame.
			@return
				False once a window has asked to close, or before init().
			*/
		bool update();

		/*!
			@brief
				Detaches the layer and releases every subsystem.

				Safe to call more than once; the destructor calls it too.
			*/
		void shutdown();

	private:

		std::unique_ptr<SystemManager>	systems_;
		std::unique_ptr<FrameLoop>		loop_;
		ApplicationLayer*				layer_{};
	};
}
