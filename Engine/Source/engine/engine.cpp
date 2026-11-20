/*************************************************************************************************************
\file       engine.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Defines VoxEngine's out-of-line constructor, destructor, init, update and shutdown; all real
            work is delegated to SystemManager, the startup checks and FrameLoop.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include <vox/engine.hpp>

// Engine
#include "systems/system_manager.hpp"
#include "runtime/frame_loop.hpp"
#include "checks/startup.hpp"

// Only for GLFWwindow in onAttach
#include "../window/sys/win_sys.hpp"

// Profiling
#include "../profile/profiler.hpp"


namespace engine
{
	VoxEngine::VoxEngine() = default;

	/*!
		@brief
			Shuts the engine down if it has not been already.
		*/
	VoxEngine::~VoxEngine()
	{
		shutdown();
	}

	/*!
		@brief
			Starts the profiler, initialises every subsystem, runs the startup
			checks and scripts, attaches the layer and builds the frame loop.
		@param layer
			Presentation layer to attach, or null for Game (which also spawns the
			physics test scene)
		@param assetRoot
			Folder assets are loaded relative to
		@param initialScene
			Scene to load at startup, or empty for none
		*/
	void VoxEngine::init(ApplicationLayer* layer,
		const std::filesystem::path& assetRoot, const std::filesystem::path& initialScene)
	{
		// First: names the main thread and the capture before any zone opens.
		profile::startup(layer ? "Editor" : "Game");

		systems_ = std::make_unique<SystemManager>();
		systems_->init(assetRoot, initialScene);

		// The demo arena is for Game only; in the Editor it would mix into
		// whatever scene is open.
		checks::runStartup(*systems_, layer == nullptr && initialScene.empty());

		systems_->startScripts();

		layer_ = layer;
		if (layer_)
		{
			layer_->onAttach(systems_->windowSys().windows().front(), systems_->ecsManager(),
				systems_->lua(), systems_->scenes(), systems_->resources(), systems_->bus(), systems_->voxelSys(),
				systems_->graphicsSys());
		}

		loop_ = std::make_unique<FrameLoop>(*systems_, layer_);
	}

	/*!
		@brief
			Runs one frame.
		@return
			False once a window asks to close, or if the engine was never initialised
		*/
	bool VoxEngine::update()
	{
		return loop_ && loop_->tick();
	}

	/*!
		@brief
			Detaches the layer, drops the frame loop and shuts every subsystem
			down. Idempotent.
		*/
	void VoxEngine::shutdown()
	{
		if (layer_)
		{
			layer_->onDetach();
			layer_ = nullptr;
		}

		loop_.reset();

		if (systems_)
			systems_->shutdown();
	}
}
