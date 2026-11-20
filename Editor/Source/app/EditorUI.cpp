/*************************************************************************************************************
\file       EditorUI.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      EditorUI core: attach / detach (ImGui context, backends, self-checks, preferences), the per-frame
            interface build, frame pacing and the idle throttle, input ownership, logging and the biome
            library reload.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "EditorUI.hpp"

// Editor
#include "EditorUICommon.hpp"

// Engine
#include "events/bus.hpp"
#include "graphics/renderer/overlay_renderer.hpp"
#include "profile/profiler_gpu.hpp"
#include "voxel/events/voxel_events.hpp"
#include "voxel/sys/voxel_sys.hpp"

// GLEW
#include <GL/glew.h>

// GLFW
#include <GLFW/glfw3.h>

// ImGui
#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// stds
#include <iostream>
#include <stdexcept>


namespace vox::editor
{
	namespace
	{
		/*!
			@brief
				Prints the outcome of one of the editor's startup self-checks.
			@param tag
				Name of the check, printed between square brackets
			@param fault
				What failed; empty when every check passed
			*/
		void reportSelfCheck(const char* tag, const std::string& fault)
		{
			if (fault.empty())
				std::cout << "[" << tag << "] all checks passed\n";
			else
				std::cerr << "[" << tag << "] FAILED: " << fault << "\n";
		}
	}

	/*!
		@brief
			Constructs the editor layer; out of line because the overlay renderer is only forward
			declared in the header.
		@param persistLayout
			Whether the docking layout, preferences and the recent-level list are kept in
			Editor/.editor between sessions
		*/
	EditorUI::EditorUI(bool persistLayout)
		:
		persistLayout_(persistLayout)
	{
	}

	EditorUI::~EditorUI() = default;

	/*!
		@brief
			Takes the engine services, creates the ImGui context and its backends, loads the layout and
			preferences, runs the editor's own self-checks and reopens the most recent level.

			Also puts the terrain in Deferred collider mode and the editor's coarser LoD, and subscribes
			to WorldLoaded so terrain undo entries are dropped with the old terrain.
		@param window
			The main window the editor draws into
		@param ecs::ECSManager&
			Unused
		@param scripting::lua::Interface&
			Unused
		@param scene
			The scene manager
		@param resources
			The resource manager; its root is the asset folder
		@param bus
			The engine event bus
		@param voxel
			The voxel terrain system
		@param graphics
			The graphics system, read for the camera
		*/
	void EditorUI::onAttach(GLFWwindow* window,
							ecs::ECSManager&,
							scripting::lua::Interface&,
							SceneManager& scene,
							ResourceManager& resources,
							events::Bus& bus,
							voxel::System& voxel,
							graphics::System& graphics)
	{
		bus_ = &bus;
		voxel_ = &voxel;
		graphics_ = &graphics;
		scene_ = &scene;
		resources_ = &resources;
		window_ = window;

		// --------------------------- Window ---------------------------

		glfwSetWindowSize(window, 1440, 900);

		// The engine input system owns GLFW's user pointer.
		windows_[window] = this;

		glfwSetWindowCloseCallback(
			window,
			[](GLFWwindow* w)
			{
				auto* editor = windows_.at(w);
				glfwSetWindowShouldClose(w, GLFW_FALSE);
				editor->request(Command::Exit);
			});

		// ------------------ ImGui context and layout ------------------

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		contextReady_ = true;

		ImGui::GetIO().IniFilename = nullptr;
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		// Viewport drags edit the scene; undocked panels move by their title bars.
		ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;

		if (persistLayout_)
		{
			const auto directory = resources.root().parent_path() / "Editor/.editor";
			std::filesystem::create_directories(directory);

			layoutFile_ = (directory / "layout.ini").string();
			ImGui::GetIO().IniFilename = layoutFile_.c_str();
			ImGui::LoadIniSettingsFromDisk(layoutFile_.c_str());

			recent_.load(directory / "recent.txt");
			prefs_.load(directory / "prefs.txt");
		}

		showHierarchy_	= prefs_.getBool("panel.hierarchy", showHierarchy_);
		showInspector_	= prefs_.getBool("panel.inspector", showInspector_);
		showAssets_		= prefs_.getBool("panel.content", showAssets_);
		showResources_	= prefs_.getBool("panel.resources", showResources_);
		showViewport_	= prefs_.getBool("panel.viewport", showViewport_);
		showConsole_	= prefs_.getBool("panel.console", showConsole_);
		showProfiler_	= prefs_.getBool("panel.profiler", showProfiler_);
		showTerrain_	= prefs_.getBool("panel.terrain", showTerrain_);
		showPipeline_	= prefs_.getBool("panel.pipeline", showPipeline_);
		limitFrameRate_	= prefs_.getBool("performance.limitFrameRate", limitFrameRate_);
		idleThrottle_	= prefs_.getBool("performance.idleThrottle", idleThrottle_);

		// No terrain collision while paused: cooking it is the slow half of every remesh, the
		// brushes aim with the voxel raycast, and nothing simulates. Play cooks what is owed.
		voxel.setColliderMode(voxel::ColliderMode::Deferred);

		// Coarser terrain LoD than the game's: render.json's "editor" block. The Terrain panel's
		// slider still moves it for the session.
		voxel.renderSettings().subDivisionSize_ = voxel.renderSettings().editorSubDivisionSize_;

		applyTheme();
		profiler_.setRepoRoot(resources.root().parent_path());

		// -------------------------- Backends --------------------------

		platformReady_ = ImGui_ImplGlfw_InitForOpenGL(window, true);
		if (!platformReady_)
			throw std::runtime_error("Editor window backend initialization failed.");

		rendererReady_ = ImGui_ImplOpenGL3_Init();
		if (!rendererReady_)
			throw std::runtime_error("Editor graphics backend initialization failed.");

		// ------------------------- Self-checks ------------------------

		// The gizmo's arithmetic is checked here because nothing can click it in an automated run.
		const std::string gizmoFault = RegionGizmo::selfCheck();
		reportSelfCheck("gizmo-check", gizmoFault);

		// Likewise where a brush goes and when it stamps.
		const std::string toolFault = TerrainToolset::selfCheck();
		reportSelfCheck("terrain-tool-check", toolFault);

		// ------------------- Overlay and subscriptions ----------------

		overlay_ = std::make_unique<graphics::renderer::OverlayRenderer>();

		worldLoaded_ = ::events::ScopedSubscription(
			bus,
			bus.subscribe<voxel::events::WorldLoaded>(
				[this](const voxel::events::WorldLoaded&)
				{
					adoptTerrain_ = true;

					// The voxel history went with the old terrain, so its strokes cannot be undone any more.
					const auto stroke = [](const UndoEntry& e)
					{
						return !e.scene;
					};

					std::erase_if(undo_, stroke);
					std::erase_if(redo_, stroke);
				}));

		// A terrain that finished loading before this subscription existed still counts.
		adoptTerrain_ = voxel.hasWorld();

		// ---------------------- Reopen last level ---------------------

		refresh();
		log(LogLevel::Info, "Editor ready. Double-click a level in the Content Browser to open it.");

		// Pick up where the last session stopped.
		if (!recent_.items().empty())
		{
			pendingPath_ = resources_->root() / recent_.items().front();
			attempt([&] { perform(Command::OpenLevel); });
		}
	}

	/*!
		@brief
			Releases every GL object and the ImGui context, while the GL context is still current, and
			forgets the engine services.
		*/
	void EditorUI::onDetach() noexcept
	{
		if (viewportDepth_)
			glDeleteRenderbuffers(1, &viewportDepth_);
		if (viewportTexture_)
			glDeleteTextures(1, &viewportTexture_);
		if (viewportFramebuffer_)
			glDeleteFramebuffers(1, &viewportFramebuffer_);

		viewportDepth_ = viewportTexture_ = viewportFramebuffer_ = 0;
		viewportWidth_ = viewportHeight_ = 0;

		// While the context is still current.
		pipeline_.release();
		overlay_.reset();
		worldLoaded_.reset();
		gizmo_.release();
		gizmoHot_ = false;
		toolsActive_ = false;

		if (window_)
			glfwSetWindowCloseCallback(window_, nullptr);

		windows_.erase(window_);
		preview_.reset();

		if (rendererReady_)
			ImGui_ImplOpenGL3_Shutdown();
		if (platformReady_)
			ImGui_ImplGlfw_Shutdown();
		if (contextReady_)
			ImGui::DestroyContext();

		rendererReady_ = platformReady_ = contextReady_ = false;

		scene_ = nullptr;
		resources_ = nullptr;
		voxel_ = nullptr;
		graphics_ = nullptr;
		window_ = nullptr;
	}

	/*!
		@brief
			Builds and draws one frame of the interface: new ImGui frame, buildInterface, render.
		@param renderScene
			Draws the scene into whatever framebuffer is bound; called from inside the Viewport panel
		*/
	void EditorUI::onFrame(const std::function<engine::SceneView()>& renderScene)
	{
		{
			VOX_PROFILE_SCOPE_C("ImGui new frame", profile::colour::kEditor);
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
            ImGuizmo::BeginFrame();
		}

		buildInterface(renderScene);

		{
			// Two costs, two lanes: building the draw lists is CPU, replaying them is GPU.
			VOX_PROFILE_SCOPE_C("ImGui render", profile::colour::kEditor);
			VOX_PROFILE_GPU_SCOPE_C("Editor UI", profile::colour::kEditor);
			ImGui::Render();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			++frames_;
		}
	}

	/*!
		@brief
			Everything between ImGui::NewFrame and ImGui::Render: watchers, shortcuts, bars, panels,
			dialogs, the window title, next frame's input ownership, frame pacing and preferences.
		@param renderScene
			Draws the scene; handed to the Viewport panel
		*/
	void EditorUI::buildInterface(const std::function<engine::SceneView()>& renderScene)
	{
		VOX_PROFILE_SCOPE_C("Editor panels", profile::colour::kEditor);

		watchTerrain();
		watchRecipe();
		shortcuts();
		menuBar();

		// Both bars shrink the viewport's work area, so they come before the dockspace.
		toolbar();
		statusBar();
		dockspace();

		hierarchy();
		inspector();
		contentPanel();
		resourcePanel();
		terrainPanel();
		profilerPanel();
		pipelinePanel();
		viewport(renderScene);

		if (showConsole_)
		{
			if (focusConsole_)
				ImGui::SetNextWindowFocus();

			if (ImGui::Begin("Console", &showConsole_))
				log_.draw();
			ImGui::End();
		}

		focusConsole_ = false;
		dialogs();

		const std::string title = "VoxEditor - " + levelName() + (levelDirty() ? " *" : "");
		if (title != title_)
		{
			title_ = title;
			glfwSetWindowTitle(window_, title_.c_str());
		}

		// Who owns the devices next frame; read by capturesKeyboard / capturesMouse.
		uiWantsKeyboard_ = ImGui::GetIO().WantTextInput || anyPopupOpen();

		const bool anyButton = ImGui::IsMouseDown(ImGuiMouseButton_Left)
			|| ImGui::IsMouseDown(ImGuiMouseButton_Right)
			|| ImGui::IsMouseDown(ImGuiMouseButton_Middle);

		if (!anyButton)
			viewportDrag_ = false;
		else if (viewportHovered_)
			viewportDrag_ = true;

		updateFramePacing();
		storePrefs();
	}

	/*!
		@brief
			Tests whether the user did anything this frame, or is holding something down.

			Looks at mouse movement and wheel, typed text, active items, held mouse buttons and keys,
			and gamepad buttons and sticks.
		@return
			True when there was any user input
		*/
	bool EditorUI::userActive() const
	{
		const ImGuiIO& io = ImGui::GetIO();

		if (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f || io.MouseWheel != 0.f || io.MouseWheelH != 0.f)
			return true;

		if (!io.InputQueueCharacters.empty() || ImGui::IsAnyItemActive())
			return true;

		for (int button = 0; button < ImGuiMouseButton_COUNT; ++button)
			if (io.MouseDown[button])
				return true;

		// A held key is no stream of events: the OS repeats it only after a pause, and the fly
		// camera reads the key's state, so the wait in the frame loop would not be cut short.
		for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
			if (ImGui::IsKeyDown(static_cast<ImGuiKey>(key)))
				return true;

		// Pads have no events at all; they are polled once a frame.
		for (int pad = GLFW_JOYSTICK_1; pad <= GLFW_JOYSTICK_LAST; ++pad)
		{
			GLFWgamepadstate state;
			if (!glfwJoystickIsGamepad(pad) || !glfwGetGamepadState(pad, &state))
				continue;

			for (const unsigned char pressed : state.buttons)
				if (pressed == GLFW_PRESS)
					return true;

			// Sticks only: a trigger rests at -1.
			for (int axis = GLFW_GAMEPAD_AXIS_LEFT_X; axis <= GLFW_GAMEPAD_AXIS_RIGHT_Y; ++axis)
				if (state.axes[axis] > 0.25f || state.axes[axis] < -0.25f)
					return true;
		}

		return false;
	}

	/*!
		@brief
			Decides how long the engine may wait before the next frame.

			A plain 60 fps cap while anything is going on, 10 fps (5 when unfocused or minimised) once
			the user has stopped touching the editor for half a second and nothing is running in the
			background; no cap when Limit to 60 fps is off. Called at the end of a frame, with that
			frame's input still in ImGui.
		*/
	void EditorUI::updateFramePacing()
	{
		// An editor has no use for more than the display shows a game at; vsync alone lets a
		// 144 Hz panel run it at 144.
		constexpr double kActiveInterval = 1.0 / 60.0;
		constexpr double kIdleInterval = 1.0 / 10.0;
		constexpr double kBackgroundInterval = 1.0 / 5.0;

		// How long after the last sign of life the editor keeps its full rate: tooltips, the
		// recipe debounce and ImGui's own settling all want a few more frames.
		constexpr double kLinger = 0.5;

		if (!limitFrameRate_)
		{
			frameInterval_ = 0.;
			return;
		}

		const double now = ImGui::GetTime();

		// Work that only moves forward when frames run: results from the workers are picked up
		// once a frame, and a simulation at 10 Hz would take 25 fixed steps a frame.
		const bool working = playing_
			|| pendingPlay_
			|| glfwGetInputMode(window_, GLFW_CURSOR) == GLFW_CURSOR_DISABLED
			|| !voxel_->quiet()
			|| pipelinePreview_.busy()
			|| pipelineDirty_
			|| saveWhenReady_
			|| adoptTerrain_
			|| panelChangedAt_ >= 0.
			|| focusContentFrames_ > 0
			|| frames_ < 30;

		if (working || userActive())
			lastActivity_ = now;

		if (!idleThrottle_ || now - lastActivity_ < kLinger)
		{
			frameInterval_ = kActiveInterval;
			return;
		}

		const bool background = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_FALSE
			|| glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE;

		frameInterval_ = background ? kBackgroundInterval : kIdleInterval;
	}

	/*!
		@brief
			Copies the panel flags and the Performance choices into prefs_ and writes them if they changed.
		*/
	void EditorUI::storePrefs()
	{
		prefs_.setBool("panel.hierarchy", showHierarchy_);
		prefs_.setBool("panel.inspector", showInspector_);
		prefs_.setBool("panel.content", showAssets_);
		prefs_.setBool("panel.resources", showResources_);
		prefs_.setBool("panel.viewport", showViewport_);
		prefs_.setBool("panel.console", showConsole_);
		prefs_.setBool("panel.profiler", showProfiler_);
		prefs_.setBool("panel.terrain", showTerrain_);
		prefs_.setBool("panel.pipeline", showPipeline_);
		prefs_.setBool("performance.limitFrameRate", limitFrameRate_);
		prefs_.setBool("performance.idleThrottle", idleThrottle_);
		prefs_.save();
	}

	/*!
		@brief
			Tells the engine whether the editor owns the keyboard for the next input update.
		@return
			True while ImGui wants text, a popup is open, or Ctrl is held
		*/
	bool EditorUI::capturesKeyboard() const
	{
		if (!window_)
			return false;

		// Ctrl is read from GLFW, not ImGui: input resolves before ImGui's frame starts, and a
		// shortcut such as Ctrl+S must not also walk the camera backwards.
		return uiWantsKeyboard_
			|| glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
			|| glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
	}

	/*!
		@brief
			Tells the engine whether the editor owns the mouse for the next input update.
		@return
			False while the cursor is captured; true while a gizmo handle is hot, while a terrain tool
			takes a left click or the B key over the viewport, or whenever the pointer is off the
			viewport and no drag began there
		*/
	bool EditorUI::capturesMouse() const
	{
		if (!window_)
			return false;

		// A captured cursor is a look drag that began in the viewport; it stays the camera's.
		if (glfwGetInputMode(window_, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
			return false;

		// A click on a gizmo handle is the editor's: left mouse is bound to gameplay actions too.
		if (gizmoHot_ || gizmoDragging_)
			return true;

		// So is a terrain tool's. Read from GLFW, which has this frame's buttons already (input
		// resolves after the poll and before ImGui's frame): the press itself has to be withheld,
		// not only the frames after it. The right button stays the camera's so a look drag can
		// still begin, and so does the wheel unless the brush-adjust key turns it into a resize.
		if (toolsActive_ && (viewportHovered_ || viewportDrag_)
			&& glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS)
		{
			if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS
				|| glfwGetKey(window_, GLFW_KEY_B) == GLFW_PRESS)
				return true;
		}

		return !viewportHovered_ && !viewportDrag_;
	}

	/*!
		@brief
			Adds a line to the Console; an error also opens and focuses the Console on the next frame.
		@param level
			Severity of the message
		@param text
			The message
		*/
	void EditorUI::log(LogLevel level, std::string text)
	{
		// Focus is deferred to onFrame: this is also called outside an ImGui frame.
		if (level == LogLevel::Error)
			showConsole_ = focusConsole_ = true;

		log_.add(level, std::move(text));
	}

	/*!
		@brief
			Runs an action, sending whatever it throws to the console and cancelling any command or
			save queued behind it.
		@param action
			The action to run
		*/
	void EditorUI::attempt(const std::function<void()>& action)
	{
		try
		{
			action();
		}
		catch (const std::exception& e)
		{
			// Whatever was queued behind this action must not run on top of a failure.
			afterSave_ = Command::None;
			saveWhenReady_ = false;
			log(LogLevel::Error, e.what());
		}
	}

	/*!
		@brief
			Rescans the asset folder and reloads the biome library.
		*/
	void EditorUI::refresh()
	{
		browser_.rescan(resources_->root());
		loadBiomeLibrary();
	}

	/*!
		@brief
			Reads voxel/biomes.json from disk into biomeLibrary_, logging and keeping the error when it
			is missing or does not parse.
		*/
	void EditorUI::loadBiomeLibrary()
	{
		// Read from disk, not through the resource cache: a biome edited in a text editor shows
		// up on the next refresh.
		biomeLibrary_.reset();
		biomeLibraryError_.clear();
		terrain_.regionMoved(); // whatever it had composed came from the old library

		try
		{
			const auto bytes = readFileBytes(resources_->resolve("voxel/biomes.json"));
			if (!bytes)
				throw std::runtime_error("Assets/voxel/biomes.json is missing.");

			biomeLibrary_ = voxel::BiomeLibrary::parse(*bytes);
		}
		catch (const std::exception& e)
		{
			biomeLibraryError_ = e.what();
			log(LogLevel::Error, "Biome library: " + biomeLibraryError_);
		}
	}
}
