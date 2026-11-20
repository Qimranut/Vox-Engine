/*************************************************************************************************************
\file       system_manager.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements engine::SystemManager: constructs every subsystem, initialises them in dependency
            order (events wired before the window exists), starts the scripts and shuts down in order.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "system_manager.hpp"

// Subsystems
#include "../../platform/platform_sys.hpp"
#include "../../window/sys/win_sys.hpp"
#include "../../events/bus.hpp"
#include "../../input/sys/input_sys.hpp"
#include "../../lua/interface/lua_interface.hpp"
#include "../../lua/bindings/lua_input_events.hpp"
#include "../../lua/bindings/lua_api.hpp"
#include "../../ecs/ecs_manager/ecs_mng.hpp"
#include "../../graphics/sys/gph_sys.hpp"
#include "../../physics/sys/phy_sys.hpp"
#include "../../audio/sys/audio_sys.hpp"
#include "../../voxel/sys/voxel_sys.hpp"
#include "../../voxel/render/voxel_renderer.hpp"
#include "../../scheduler/scheduler.hpp"
#include "../../serializer/sys/serializer_sys.hpp"
#include "../../editor/sys/editor_sys.hpp"
#include "scene/SceneManager.hpp"

// ECS saves
#include "../../directories/saves.hpp"

// Cross-subsystem links
#include "../wiring/event_wiring.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <stdexcept>


namespace engine
{
	SystemManager::SystemManager() = default;

	/*!
		@brief
			Shuts every subsystem down.
		*/
	SystemManager::~SystemManager()
	{
		shutdown();
	}

	/*!
		@brief
			Constructs every subsystem and initialises them in dependency order.

			The event wiring is connected before graphics init, because the bus has
			no replay and the window publishes WindowCreated while being created.
		@param assetRoot
			Root the ResourceManager loads relative to
		@param initialScene
			Scene loaded after the empty one; empty for none
		*/
	void SystemManager::init(const std::filesystem::path& assetRoot, const std::filesystem::path& initialScene)
	{
		VOX_PROFILE_SCOPE("Engine init");

		construct();

		ecs_->registerArchetypes();

		// SceneManager loads the active scene. Restoring the whole registry here
		// would add saved objects that survive every scene change.
		serializer_->registerComponents();

		resources_ = std::make_unique<vox::ResourceManager>(assetRoot);
		scenes_ = std::make_unique<vox::SceneManager>(ecs_->registry(), *resources_, *bus_);
		scenes_->newScene();
		if (!initialScene.empty()) scenes_->load(initialScene);

		// The bus has no replay: this must stay ahead of graphicsInit(), where
		// the window publishes WindowCreated.
		wiring::connectEvents(*this);

		graphicsInit();
		audio_->init(*bus_, ecs_->registry(), *resources_);
		physicsInit();
		voxelInit();
		schedulerSetup();

		luaInterface_->init();
		scripting::lua::registerEngineApi(*this);
	}

	/*!
		@brief
			Clears Lua's input callbacks, reloads every script in Scripts/, runs
			their OnCreate and reads the exposed variables.
		*/
	void SystemManager::startScripts()
	{
		VOX_PROFILE_SCOPE_C("Start scripts", profile::colour::kScript);

		// Lua OnCreate / OnUpdate / OnDestroy

		// Callbacks point into the environments reload() is about to replace.
		luaInputEvents_->clear();
		luaInterface_->reload();

		luaInterface_->invokeGlobal("OnCreate");
		//luaInterface_->invokeGlobal("OnUpdate");
		//luaInterface_->invokeGlobal("OnDestroy");

		// Lua Exposed vars
		luaInterface_->retrieveVars();
	}

	/*!
		@brief
			Saves the ECS, then releases subsystems that need an explicit shutdown,
			in order: workers, voxel, scene, physics, audio, graphics, window.

			Safe to call more than once, and after a partial init().
		*/
	void SystemManager::shutdown()
	{
		VOX_PROFILE_SCOPE("Engine shutdown");

		try
		{
			serializer_->save(ecs_->registry(), std::string(dir::save::files) + "ecs_save_1");
		}
		catch (const std::exception& e)
		{
			std::cerr << "ECS save failed: " << e.what() << std::endl;
		}
		catch (...)
		{
			std::cerr << "ECS save failed: unknown exception" << std::endl;
		}

		// Join workers before destroying any systems they access.
		graphicsWorker_.reset();
		// Voxel first: it joins its own workers, pulls its bodies out of the
		// physics world and frees GL while the context is still alive.
		if (voxel_)
		{
			if (gph_) gph_->setWorldPass(nullptr);
			voxel_->shutdown();
		}
		if (scenes_) scenes_->unload();
		if (phys_ && physicsReady_)
		{
			phys_->shutdown();
			physicsReady_ = false;
		}
		if (audio_) audio_->shutdown();
		// Release GPU buffers while the window's OpenGL context still exists.
		gph_.reset();
		if (win_) win_->shutdown();
	}

	/*!
		@brief
			Constructs every subsystem object and the graphics worker, without
			initialising them.
		*/
	void SystemManager::construct()
	{
		platform_				= std::make_unique<platform::System>();
		bus_					= std::make_unique<events::Bus>();
		win_					= std::make_unique<window::System>(*bus_);
		input_					= std::make_unique<input::System>(*bus_);
		luaInterface_			= std::make_unique<scripting::lua::Interface>();
		luaInputEvents_			= std::make_unique<scripting::lua::InputEventBridge>(*bus_);
		ecs_					= std::make_unique<ecs::ECSManager>();
		gph_					= std::make_unique<graphics::System>(*win_, *ecs_);
		phys_					= std::make_unique<physics::System>(*bus_, *ecs_);
		audio_					= std::make_unique<audio::System>();
		voxel_					= std::make_unique<voxel::System>(*bus_, *ecs_, *phys_);
		serializer_				= std::make_unique<serializer::System>();
		editor_					= std::make_unique<editor::System>(*ecs_);

		scheduler_				= std::make_unique<scheduler::Scheduler>();
		graphicsWorker_			= std::make_unique<scheduler::SystemWorker>(gph_.get());
	}

	/*!
		@brief
			Registers serializable components and loads every ECS save.
		*/
	void SystemManager::serializationInit()
	{
		serializer_->registerComponents();

		serializer_->loadAllSaves(ecs_->registry());
	}

	/*!
		@brief
			Creates the window, turns vsync on and initialises graphics. Throws if
			the window cannot be created.
		*/
	void SystemManager::graphicsInit()
	{
		VOX_PROFILE_SCOPE_C("Graphics init", profile::colour::kGraphics);

		// Window
		bool result = win_->createWindow();
		if (!result)
			throw std::runtime_error("Failed to create the window");
		glfwSwapInterval(1);
		gph_->init(*resources_);
	}

	/*!
		@brief
			Initialises physics and links the camera's collision probe.
		*/
	void SystemManager::physicsInit()
	{
		VOX_PROFILE_SCOPE_C("Physics init", profile::colour::kPhysics);

		phys_->init();

		wiring::linkCameraCollision(*this);

		physicsReady_ = true;
	}

	/*!
		@brief
			Initialises the voxel system, installs its renderer as the world pass
			and hands it the initial scene's world, which loaded before it existed.
		*/
	void SystemManager::voxelInit()
	{
		VOX_PROFILE_SCOPE_C("Voxel init", profile::colour::kVoxel);

		voxel_->init(*gph_, *resources_);
		gph_->setWorldPass(voxel_->renderer());

		// The initial scene loaded before this system existed and the bus has
		// no replay, so hand it the scene's world reference once by hand.
		voxel_->syncWorld(scenes_->world());
	}

	/*!
		@brief
			Registers systems with the scheduler; currently none.
		*/
	void SystemManager::schedulerSetup()
	{
		// Audio is NOT scheduled: its update publishes SourceCommand on the
		// bus, which is main-thread only. FrameLoop updates it directly.
		//scheduler_->registerSys(graphicsWorker_.get(), {});
	}
}
