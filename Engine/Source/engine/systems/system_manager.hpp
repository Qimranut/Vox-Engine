/*************************************************************************************************************
\file       system_manager.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares engine::SystemManager, which owns every engine subsystem plus the resource and
            scene managers, brings them up and down in order, and exposes them through accessors.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <memory>
#include <filesystem>


// Subsystems, forward declared - the owning .cpp includes them
namespace platform	 { class System; }
namespace window	 { class System; }
namespace events	 { class Bus; }
namespace input		 { class System; }
namespace ecs		 { class ECSManager; }
namespace graphics	 { class System; }
namespace physics	 { class System; }
namespace audio		 { class System; }
namespace voxel		 { class System; }
namespace serializer { class System; }
namespace editor	 { class System; }
namespace vox		 { class SceneManager; class ResourceManager; }

namespace scheduler
{
	class Scheduler;
	class SystemWorker;
}

namespace scripting::lua
{
	class Interface;
	class InputEventBridge;
}


namespace engine
{
	/**
	 * Owns every engine subsystem and brings them up and down in order.
	 *
	 * VoxEngine holds one of these and nothing else of substance. Bus
	 * subscriptions between subsystems live in engine/wiring, the Lua API in
	 * lua/bindings/lua_api*, and the per-frame ordering in
	 * engine/runtime/frame_loop.
	 *
	 * Member declaration order is destruction order - see the members below.
	 *
	 */
	class SystemManager
	{
	public:

		/*!
			@brief
				Creates an empty manager; nothing exists until init().
			*/
		SystemManager();

		/*!
			@brief
				Shuts every subsystem down.
			*/
		~SystemManager();

		SystemManager(const SystemManager&)				= delete;
		SystemManager& operator=(const SystemManager&)	= delete;

		/*!
			@brief
				Constructs every subsystem and initialises them in dependency order.
			@param assetRoot
				Root the ResourceManager loads relative to
			@param initialScene
				Scene loaded after the empty one; empty for none
			*/
		void init(const std::filesystem::path& assetRoot, const std::filesystem::path& initialScene);

		/*!
			@brief
				Loads every script in Scripts/ and runs its OnCreate, then reads
				the exposed variables.
			*/
		void startScripts();

		/*!
			@brief
				Releases subsystems that need an explicit shutdown, in order.

				Safe to call more than once, and after a partial init().
			*/
		void shutdown();

		// Accessors - only valid after init(). Suffixed so they never shadow
		// the subsystem namespaces of the same name.

		/*!
			@brief
				Gives the event bus.
			@return
				The bus
			*/
		events::Bus&						bus()			{ return *bus_; }

		/*!
			@brief
				Gives the window system.
			@return
				The window system
			*/
		window::System&						windowSys()		{ return *win_; }

		/*!
			@brief
				Gives the input system.
			@return
				The input system
			*/
		input::System&						inputSys()		{ return *input_; }

		/*!
			@brief
				Gives the Lua interface.
			@return
				The Lua interface
			*/
		scripting::lua::Interface&			lua()			{ return *luaInterface_; }

		/*!
			@brief
				Gives the bridge that owns Lua's input subscriptions.
			@return
				The Lua input event bridge
			*/
		scripting::lua::InputEventBridge&	luaEvents()		{ return *luaInputEvents_; }

		/*!
			@brief
				Gives the ECS manager.
			@return
				The ECS manager
			*/
		ecs::ECSManager&					ecsManager()	{ return *ecs_; }

		/*!
			@brief
				Gives the graphics system.
			@return
				The graphics system
			*/
		graphics::System&					graphicsSys()	{ return *gph_; }

		/*!
			@brief
				Gives the physics system.
			@return
				The physics system
			*/
		physics::System&					physicsSys()	{ return *phys_; }

		/*!
			@brief
				Gives the audio system.
			@return
				The audio system
			*/
		audio::System&						audioSys()		{ return *audio_; }

		/*!
			@brief
				Gives the voxel terrain system.
			@return
				The voxel system
			*/
		voxel::System&						voxelSys()		{ return *voxel_; }

		/*!
			@brief
				Gives the scheduler.
			@return
				The scheduler
			*/
		scheduler::Scheduler&				schedulerSys()	{ return *scheduler_; }

		/*!
			@brief
				Gives the serializer system.
			@return
				The serializer system
			*/
		serializer::System&					serializerSys()	{ return *serializer_; }

		/*!
			@brief
				Gives the engine-side editor system (the node tree).
			@return
				The editor system
			*/
		editor::System&						editorSys()		{ return *editor_; }

		/*!
			@brief
				Gives the resource manager.
			@return
				The resource manager
			*/
		vox::ResourceManager&				resources()		{ return *resources_; }

		/*!
			@brief
				Gives the scene manager.
			@return
				The scene manager
			*/
		vox::SceneManager&					scenes()		{ return *scenes_; }

	private:

		/*!
			@brief
				Constructs every subsystem object and the graphics worker, without
				initialising them.
			*/
		void construct();

		/*!
			@brief
				Registers serializable components and loads every ECS save.
			*/
		void serializationInit();

		/*!
			@brief
				Creates the window, turns vsync on and initialises graphics.
			*/
		void graphicsInit();

		/*!
			@brief
				Initialises physics and links the camera's collision probe.
			*/
		void physicsInit();

		/*!
			@brief
				Initialises the voxel system, installs its renderer as the world
				pass and hands it the initial scene's world.
			*/
		void voxelInit();

		/*!
			@brief
				Registers systems with the scheduler; currently none.
			*/
		void schedulerSetup();

		// Platform
		std::unique_ptr<platform::System> platform_;

		// Window
		std::unique_ptr<window::System> win_;

		// Event Bus
		std::unique_ptr<events::Bus> bus_;

		// Input
		std::unique_ptr<input::System> input_;

		// Scripting
		std::unique_ptr<scripting::lua::Interface> luaInterface_;

		// Lua input subscriptions. Declared after luaInterface_ so it is
		// destroyed first, while the Lua state is still open.
		std::unique_ptr<scripting::lua::InputEventBridge> luaInputEvents_;

		// Entity Registry
		std::unique_ptr<ecs::ECSManager> ecs_;

		// Graphics
		std::unique_ptr<graphics::System> gph_;

		// Physics
		std::unique_ptr<physics::System> phys_;

		// Audio
		std::unique_ptr<audio::System> audio_;

		// Voxel world. After graphics and physics because it draws through
		// one and puts bodies into the other; shutdown() also stops it
		// before either of them.
		std::unique_ptr<voxel::System> voxel_;

		// Scheduler
		std::unique_ptr<scheduler::Scheduler> scheduler_;

		// System workers. Physics has none: it steps inside the fixed
		// timestep on the main thread and parallelises through Jolt's own
		// job system - see physics/sys/phy_sys.hpp. Audio has none either:
		// its update publishes on the main-thread-only bus.
		std::unique_ptr<scheduler::SystemWorker> graphicsWorker_;

		// Serializer
		std::unique_ptr<serializer::System> serializer_;

		// Editor
		std::unique_ptr<editor::System> editor_;

		bool physicsReady_{};

		std::unique_ptr<vox::ResourceManager> resources_;
		std::unique_ptr<vox::SceneManager> scenes_;
	};
}
