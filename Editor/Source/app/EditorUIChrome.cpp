/*************************************************************************************************************
\file       EditorUIChrome.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      EditorUI chrome: the editor's ImGui theme, keyboard shortcuts, main menu bar, toolbar with
            Play / Pause, status bar (level, terrain, last log line, frame rate) and the default docking layout.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "EditorUI.hpp"

// Editor
#include "EditorUICommon.hpp"

// Engine
#include "voxel/sys/voxel_sys.hpp"

// ImGui
#include <imgui.h>
#include <imgui_internal.h>

// stds
#include <algorithm>
#include <cstdio>


namespace vox::editor
{
	/*!
		@brief
			Applies the editor's look to the current ImGui context.

			Neutral grey panels with one green accent. Every colour comes from the five values at the
			top, so retinting the editor means changing those.
		*/
	void applyTheme()
	{
		const auto grey = [](float v, float a = 1.0f)
		{
			return ImVec4(v, v, v, a);
		};

		const ImVec4 background	= grey(0.17f);	// panels
		const ImVec4 recessed	= grey(0.12f);	// text boxes, lists, the area behind docked panels
		const ImVec4 raised		= grey(0.25f);	// buttons, headers, inactive tabs
		const ImVec4 text		= grey(0.86f);
		const ImVec4 accent(0.18f, 0.55f, 0.30f, 1.0f);

		const auto lighter = [](ImVec4 c, float by)
		{
			return ImVec4(c.x + by, c.y + by, c.z + by, c.w);
		};

		const auto faded = [](ImVec4 c, float alpha)
		{
			return ImVec4(c.x, c.y, c.z, alpha);
		};

		// --------------------------- Metrics --------------------------

		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();

		style.WindowRounding	= 0;
		style.FrameRounding		= 3;
		style.TabRounding		= 3;
		style.GrabRounding		= 3;
		style.PopupRounding		= 3;
		style.ScrollbarRounding	= 3;
		style.WindowBorderSize	= 1;
		style.FrameBorderSize	= 0;
		style.FramePadding		= ImVec2(6, 4);
		style.ItemSpacing		= ImVec2(8, 5);

		// --------------------------- Colours --------------------------

		ImVec4* c = style.Colors;

		c[ImGuiCol_Text]						= text;
		c[ImGuiCol_TextDisabled]				= grey(0.50f);
		c[ImGuiCol_WindowBg]					= background;
		c[ImGuiCol_ChildBg]						= faded(recessed, 0.55f);
		c[ImGuiCol_PopupBg]						= lighter(background, 0.02f);
		c[ImGuiCol_Border]						= grey(0.08f);
		c[ImGuiCol_FrameBg]						= recessed;
		c[ImGuiCol_FrameBgHovered]				= lighter(recessed, 0.06f);
		c[ImGuiCol_FrameBgActive]				= lighter(recessed, 0.10f);
		c[ImGuiCol_TitleBg]						= recessed;
		c[ImGuiCol_TitleBgActive]				= lighter(recessed, 0.03f);
		c[ImGuiCol_TitleBgCollapsed]			= recessed;
		c[ImGuiCol_MenuBarBg]					= lighter(recessed, 0.02f);
		c[ImGuiCol_ScrollbarBg]					= faded(recessed, 0.60f);
		c[ImGuiCol_ScrollbarGrab]				= raised;
		c[ImGuiCol_ScrollbarGrabHovered]		= lighter(raised, 0.08f);
		c[ImGuiCol_ScrollbarGrabActive]			= lighter(raised, 0.16f);
		c[ImGuiCol_CheckMark]					= lighter(accent, 0.25f);
		c[ImGuiCol_SliderGrab]					= accent;
		c[ImGuiCol_SliderGrabActive]			= lighter(accent, 0.15f);
		c[ImGuiCol_Button]						= raised;
		c[ImGuiCol_ButtonHovered]				= lighter(raised, 0.08f);
		c[ImGuiCol_ButtonActive]				= accent;
		c[ImGuiCol_Header]						= faded(accent, 0.75f);	// selected rows and tiles
		c[ImGuiCol_HeaderHovered]				= faded(accent, 0.45f);
		c[ImGuiCol_HeaderActive]				= accent;
		c[ImGuiCol_Separator]					= grey(0.08f);
		c[ImGuiCol_SeparatorHovered]			= faded(accent, 0.80f);
		c[ImGuiCol_SeparatorActive]				= accent;
		c[ImGuiCol_ResizeGrip]					= faded(raised, 0.60f);
		c[ImGuiCol_ResizeGripHovered]			= faded(accent, 0.70f);
		c[ImGuiCol_ResizeGripActive]			= accent;
		c[ImGuiCol_Tab]							= recessed;

		// The open tab of every panel is filled with the accent, so what is showing is obvious at
		// a glance: full strength in the panel that has focus, darker in the others.
		const ImVec4 dimAccent(accent.x * 0.62f, accent.y * 0.62f, accent.z * 0.62f, 1.0f);

		c[ImGuiCol_TabHovered]					= lighter(accent, 0.10f);
		c[ImGuiCol_TabSelected]					= accent;
		c[ImGuiCol_TabSelectedOverline]			= lighter(accent, 0.25f);
		c[ImGuiCol_TabDimmed]					= recessed;
		c[ImGuiCol_TabDimmedSelected]			= dimAccent;
		c[ImGuiCol_TabDimmedSelectedOverline]	= dimAccent;
		c[ImGuiCol_DockingPreview]				= faded(accent, 0.55f);
		c[ImGuiCol_DockingEmptyBg]				= recessed;
		c[ImGuiCol_PlotHistogram]				= accent;				// progress bars
		c[ImGuiCol_PlotHistogramHovered]		= lighter(accent, 0.12f);
		c[ImGuiCol_TextSelectedBg]				= faded(accent, 0.50f);
		c[ImGuiCol_DragDropTarget]				= lighter(accent, 0.30f);
		c[ImGuiCol_NavCursor]					= lighter(accent, 0.20f);
		c[ImGuiCol_ModalWindowDimBg]			= ImVec4(0, 0, 0, 0.55f);
	}

	/*!
		@brief
			Handles the global keyboard shortcuts: Ctrl+N/O/S/Shift+S, Ctrl+P, Ctrl+Z/Y and Home over the
			viewport. Does nothing while a popup is open.
		*/
	void EditorUI::shortcuts()
	{
		// Global routing yields to an active text box, so Ctrl+Z there still undoes typing.
		if (anyPopupOpen())
			return;

		const ImGuiInputFlags route = ImGuiInputFlags_RouteGlobal;

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, route))
			request(Command::NewLevel);
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, route))
			askOpenLevel_ = true;
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, route) && scene_->loaded())
			attempt([&] { saveLevel(); });
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, route) && scene_->loaded())
			openSaveAs();

		// Home frames the terrain region. Not a letter: scripts are live in the editor, and letters
		// are theirs.
		if (ImGui::IsKeyPressed(ImGuiKey_Home, false) && !ImGui::GetIO().WantTextInput
			&& !ImGui::GetIO().KeyCtrl && viewportHovered_)
			frameRegion();

		// With Ctrl, because plain letters belong to the scripts, which are live in the editor.
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, route))
			togglePlay();

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, route))
			undo();
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, route)
			|| ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, route))
			redo();
	}

	/*!
		@brief
			Draws the main menu bar: File (levels, recent, save, close, exit), Edit (undo / redo) and
			Window (panels, Performance, Reset layout).
		*/
	void EditorUI::menuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Level", "Ctrl+N"))
				request(Command::NewLevel);
			if (ImGui::MenuItem("Open Level...", "Ctrl+O"))
				askOpenLevel_ = true;

			if (ImGui::BeginMenu("Open Recent", !recent_.items().empty()))
			{
				// Copied: opening a level reorders the list being walked.
				const std::vector<std::string> items = recent_.items();

				for (const auto& relative : items)
					if (ImGui::MenuItem(relative.c_str(), nullptr, false, relative != levelRelative()))
						request(Command::OpenLevel, resources_->root() / relative);

				ImGui::EndMenu();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Save", "Ctrl+S", false, scene_->loaded()))
				attempt([&] { saveLevel(); });
			if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, scene_->loaded()))
				openSaveAs();

			ImGui::Separator();

			if (ImGui::MenuItem("Close Level", nullptr, false, scene_->loaded()))
				request(Command::CloseLevel);
			if (ImGui::MenuItem("Exit"))
				request(Command::Exit);

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undo_.empty()))
				undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redo_.empty()))
				redo();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
			ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
			ImGui::MenuItem("Inspector", nullptr, &showInspector_);
			ImGui::MenuItem("Content Browser", nullptr, &showAssets_);
			ImGui::MenuItem("Resources", nullptr, &showResources_);
			ImGui::MenuItem("Viewport", nullptr, &showViewport_);
			ImGui::MenuItem("Console", nullptr, &showConsole_);
			ImGui::MenuItem("Terrain", nullptr, &showTerrain_);
			ImGui::MenuItem("Generation Pipeline", nullptr, &showPipeline_);
			ImGui::MenuItem("Profiler", nullptr, &showProfiler_);

			ImGui::Separator();

			if (ImGui::BeginMenu("Performance"))
			{
				ImGui::MenuItem("Limit to 60 fps", nullptr, &limitFrameRate_);
				ImGui::MenuItem("Slow down when idle", nullptr, &idleThrottle_, limitFrameRate_);
				ImGui::Separator();
				ImGui::TextDisabled("Idle: no input for half a second, not playing,\nand no terrain work in the background.");
				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Reset layout"))
				resetLayout_ = true;

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	/*!
		@brief
			Play, Pause, or Cancel while Play waits.

			Play does not start at once: it ends any open terrain stroke, switches the terrain to Live
			colliders, and toolbar() starts the simulation once voxel::System::collidersReady(). Pause
			and Cancel go back to Deferred colliders.
		*/
	void EditorUI::togglePlay()
	{
		if (playing_ || pendingPlay_)
		{
			const bool was = playing_;
			playing_ = pendingPlay_ = false;
			voxel_->setColliderMode(voxel::ColliderMode::Deferred);
			log(LogLevel::Info, was ? "Paused." : "Play cancelled.");
			return;
		}

		// A stroke in progress ends here, so its collision is part of what gets built.
		ToolContext tool = toolContext();
		tools_.deactivate(tool);

		voxel_->setColliderMode(voxel::ColliderMode::Live);
		pendingPlay_ = true;
		log(LogLevel::Info, "Building the terrain's collision...");
	}

	/*!
		@brief
			Draws the toolbar under the menu (level, history, Play / Pause buttons) and starts a pending
			Play once the terrain's colliders are ready.
		*/
	void EditorUI::toolbar()
	{
		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
		const float height = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2;

		if (ImGui::BeginViewportSideBar("##Toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, height, flags))
		{
			const auto button = [](const char* label, const char* tip, bool enabled)
			{
				ImGui::BeginDisabled(!enabled);
				const bool pressed = ImGui::Button(label);
				ImGui::EndDisabled();

				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
					ImGui::SetTooltip("%s", tip);

				ImGui::SameLine();

				return pressed;
			};

			// Level
			if (button("New", "New level (Ctrl+N)", true))
				request(Command::NewLevel);
			if (button("Open", "Open a level (Ctrl+O)", true))
				askOpenLevel_ = true;
			if (button(
					levelDirty() ? "Save *" : "Save",
					"Save the scene and its terrain (Ctrl+S)",
					scene_->loaded()))
				attempt([&] { saveLevel(); });
			if (button("Save As", "Save under a new name (Ctrl+Shift+S)", scene_->loaded()))
				openSaveAs();

			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();

			// History
			if (button("Undo", "Undo (Ctrl+Z)", !undo_.empty()))
				undo();
			if (button("Redo", "Redo (Ctrl+Y)", !redo_.empty()))
				redo();

			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();

			// Play / Pause
			// The terrain has no collision while paused (ColliderMode::Deferred), so Play first has
			// the voxel system cook what it owes - everything after a load, otherwise only what was
			// edited since the last Play - and the simulation starts when the ground is there.
			if (button(
					playing_ ? "Pause" : pendingPlay_ ? "Cancel" : "Play",
					playing_       ? "Pause physics and audio (Ctrl+P)"
					: pendingPlay_ ? "Stop waiting for the terrain's collision (Ctrl+P)"
					               : "Run physics and audio (Ctrl+P)",
					true))
				togglePlay();

			if (pendingPlay_ && voxel_->collidersReady())
			{
				pendingPlay_ = false;
				playing_ = true;
				log(LogLevel::Info, "Playing.");
			}

			ImGui::TextDisabled(playing_ ? "Playing" : pendingPlay_ ? "Building terrain collision..." : "Paused");
		}
		ImGui::End();
	}

	/*!
		@brief
			Draws the status bar: level name, terrain state or progress, the last log line (click for the
			Console), a Debug build chip and the frame rate (click for the Profiler).
		*/
	void EditorUI::statusBar()
	{
		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
		const float height = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2;

		if (ImGui::BeginViewportSideBar(
				"##StatusBar",
				ImGui::GetMainViewport(),
				ImGuiDir_Down,
				height,
				flags))
		{
			// --------------------------- Level ----------------------------

			ImGui::AlignTextToFramePadding();
			ImGui::Text("%s%s", levelName().c_str(), levelDirty() ? " *" : "");
			ImGui::SameLine(0, 16);

			// -------------------------- Terrain ---------------------------

			const voxel::Progress progress = voxel_->progress();
			const std::string& world = scene_->world();

			if (progress.busy_)
			{
				ImGui::Text("Terrain: %s", progress.stage_.c_str());
				ImGui::SameLine();
				ImGui::ProgressBar(progress.fraction_, ImVec2(160, 0));
			}
			else if (voxel_->hasWorld())
				ImGui::Text(
					"Terrain: %s%s",
					world.empty() ? "not saved yet" : world.c_str(),
					voxel_->modified() ? " (modified)" : "");
			else
				ImGui::TextDisabled(
					world.empty() ? "Terrain: none" : "Terrain: %s (not loaded)",
					world.c_str());

			// ---------------------- Last log message ----------------------

			if (!log_.empty())
			{
				ImGui::SameLine(0, 24);

				const LogLevel level = log_.lastLevel();
				ImGui::PushStyleColor(
					ImGuiCol_Text,
					level == LogLevel::Error ? kErrorText
					: level == LogLevel::Warn ? kWarningText
					: ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextUnformatted(log_.lastText().c_str());
				ImGui::PopStyleColor();

				if (ImGui::IsItemClicked())
					showConsole_ = focusConsole_ = true;
			}

			// ------------------------- Frame rate -------------------------

			// Right-aligned, always visible: the frame rate is the one number worth a glance at any
			// time. Coloured against 60 fps; the Profiler panel has the budget selector.
			const profile::FrameSummary& frame = profiler_.liveSummary();

			// Slowed down on purpose (Window > Performance): a low number here is not a slow editor.
			const bool idling = frameInterval_ > 1.0 / 30.0;

			char text[64];
			std::snprintf(text, sizeof(text), "%s%.0f fps  %.1f ms", idling ? "idle  " : "", frame.fps_, frame.averageMs_);

#ifdef _DEBUG
			// Brush stamps, generation and the rest of the engine run several times slower
			// unoptimised; nobody should judge the editor's speed by this build.
			constexpr const char* kBuildChip = "DEBUG BUILD";
			const float chipWidth = ImGui::CalcTextSize(kBuildChip).x + 16.f;
#else
			const float chipWidth = 0.f;
#endif

			const float width = ImGui::CalcTextSize(text).x;
			ImGui::SameLine(std::max(
				ImGui::GetCursorPosX(),
				ImGui::GetWindowWidth() - width - chipWidth - ImGui::GetStyle().WindowPadding.x));

#ifdef _DEBUG
			ImGui::PushStyleColor(ImGuiCol_Text, kWarningText);
			ImGui::TextUnformatted(kBuildChip);
			ImGui::PopStyleColor();

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip(
					"Unoptimised build: terrain brushes and generation run 2-4x slower.\n"
					"For everyday editing run the Release build: pick Release in Visual Studio, or build.bat release.");

			ImGui::SameLine(0, 16);
#endif

			ImGui::PushStyleColor(
				ImGuiCol_Text,
				idling || frame.averageMs_ <= 1000.0f / 60.0f + 0.5f
					? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
					: kWarningText);
			ImGui::TextUnformatted(text);
			ImGui::PopStyleColor();

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip(
					"Average over the last %zu frames. Click for the Profiler.%s",
					frame.frames_,
					idling ? "\nIdle: the editor redraws less while nothing is happening (Window > Performance)." : "");
			if (ImGui::IsItemClicked())
				showProfiler_ = focusProfiler_ = true;
		}
		ImGui::End();
	}

	/*!
		@brief
			Draws the dockspace over the main viewport, building the default layout the first time or
			after Reset layout.
		*/
	void EditorUI::dockspace()
	{
		const auto* viewport = ImGui::GetMainViewport();

		// Renamed with the Content Browser, so a layout saved for the old panels is rebuilt once.
		// Again with the Profiler, with the Terrain panel, and with the Generation Pipeline.
		const auto id = ImGui::GetID("VoxEditorDockspace5");

		if (resetLayout_ || !ImGui::DockBuilderGetNode(id))
		{
			showHierarchy_ = showInspector_ = showAssets_ = showResources_ = showViewport_ = showConsole_ =
				showProfiler_ = showTerrain_ = showPipeline_ = true;

			ImGui::DockBuilderRemoveNode(id);
			ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);

			ImGuiID center = id;
			const auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.32f, nullptr, &center);
			const auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
			const auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);

			ImGui::DockBuilderDockWindow("Hierarchy", left);
			ImGui::DockBuilderDockWindow("Inspector", right);
			ImGui::DockBuilderDockWindow("Terrain", right);
			ImGui::DockBuilderDockWindow("Viewport", center);
			ImGui::DockBuilderDockWindow("Content Browser", bottom);
			ImGui::DockBuilderDockWindow("Console", bottom);
			ImGui::DockBuilderDockWindow("Resources", bottom);
			ImGui::DockBuilderDockWindow("Profiler", bottom);
			ImGui::DockBuilderDockWindow("Generation Pipeline", bottom);
			ImGui::DockBuilderFinish(id);

			resetLayout_ = false;

			// The last window docked into a node wins its tab; bring the browser forward
			// once the nodes exist.
			focusContentFrames_ = 2;
		}

		ImGui::DockSpaceOverViewport(id, viewport);
	}
}
