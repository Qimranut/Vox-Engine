/*************************************************************************************************************
\file       EditorUI.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares EditorUI, the Editor's ApplicationLayer: menu, toolbar, status bar, panels, popups and
            the scene viewport. One class implemented across the EditorUI*.cpp files, split by concern.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Engine
#include <vox/engine.hpp>
#include "events/bus.hpp"
#include "scene/SceneManager.hpp"

// Editor
#include "ContentBrowser.hpp"
#include "EditorSettings.hpp"
#include "FileWatcher.hpp"
#include "LogConsole.hpp"
#include "PipelinePanel.hpp"
#include "ProfilerPanel.hpp"
#include "TerrainPanel.hpp"
#include "terrain/TerrainToolset.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>


namespace voxel
{
	class System;
}

namespace graphics
{
	class System;
}

namespace graphics::renderer
{
	class OverlayRenderer;
}

namespace vox::editor
{
	/**
	 * The Editor's presentation layer: menu, toolbar, status bar, panels,
	 * popups and the viewport the scene is drawn into.
	 *
	 * A "level" is a .voxscene plus the .voxmap it references. The editor
	 * opens, saves and closes the two together; see saveLevel() and
	 * commitSave().
	 *
	 * The implementation is spread over several files by concern:
	 *   EditorUI.cpp          attach / detach, the frame, input ownership
	 *   EditorUILevel.cpp     level commands, saving, undo / redo
	 *   EditorUIChrome.cpp    theme, shortcuts, menu, toolbar, status bar, docking
	 *   EditorUIPanels.cpp    hierarchy, inspector, content, resources, profiler
	 *   EditorUITerrain.cpp   terrain panel, recipe hot reload, pipeline panel
	 *   EditorUIViewport.cpp  scene viewport, region gizmo, terrain tools
	 *   EditorUIDialogs.cpp   modal popups
	 *
	 */
	class EditorUI final : public engine::ApplicationLayer
	{
	public:

		/*!
			@brief
				Constructs the editor layer.

				The constructor and destructor are both out of line: the overlay renderer is only
				forward declared here.
			@param persistLayout
				Whether the docking layout and the recent-level list are kept in Editor/.editor
				between sessions
			*/
		explicit EditorUI(bool persistLayout = true);
		/*!
			@brief
				Destroys the editor layer; out of line so the overlay renderer's type is complete.
			*/
		~EditorUI() override;

		/*!
			@brief
				Creates the ImGui context and its backends, runs the editor's own self-checks and
				reopens the most recent level.
			@param GLFWwindow*
				The main window
			@param ecs::ECSManager&
				The ECS (unused)
			@param scripting::lua::Interface&
				The Lua interface (unused)
			@param SceneManager&
				The scene manager
			@param ResourceManager&
				The resource manager
			@param events::Bus&
				The engine event bus
			@param voxel::System&
				The voxel terrain system
			@param graphics::System&
				The graphics system, read for the camera
			*/
		void onAttach(GLFWwindow*,
					  ecs::ECSManager&,
					  scripting::lua::Interface&,
					  SceneManager&,
					  ResourceManager&,
					  events::Bus&,
					  voxel::System&,
					  graphics::System&) override;

		/*!
			@brief
				Builds and draws one frame of the interface.
			@param renderScene
				Draws the scene into whatever framebuffer is bound; called from inside the Viewport panel
			*/
		void onFrame(const std::function<engine::SceneView()>& renderScene) override;

		/*!
			@brief
				Releases every GL object and the ImGui context, while the GL context is still current.
			*/
		void onDetach() noexcept override;

		/*!
			@brief
				Tells the engine whether the simulation runs.
			@return
				True while the toolbar's Play is on
			*/
		bool	isPlaying() const override	{ return playing_; }
		/*!
			@brief
				Tells the engine whether the editor owns the keyboard for the next input update.
			@return
				True while ImGui wants text, a popup is open, or Ctrl is held
			*/
		bool	capturesKeyboard() const override;
		/*!
			@brief
				Tells the engine whether the editor owns the mouse for the next input update.
			@return
				True unless the pointer is over (or dragging from) the viewport and no gizmo or tool
				claims it
			*/
		bool	capturesMouse() const override;

		/*!
			@brief
				Returns the frame cap the last frame decided on; see updateFramePacing().
			@return
				Seconds between frame starts; 0 leaves it to vsync
			*/
		double	frameInterval() const override	{ return frameInterval_; }
		/*!
			@brief
				Returns how many frames the editor has drawn.
			@return
				The frame count since attach
			*/
		int		frameCount() const			{ return frames_; }

	private:

		// Anything that replaces or closes the level, so it has to ask about
		// unsaved changes first.
		enum class Command
		{
			None,
			NewLevel,
			OpenLevel,
			CloseLevel,
			UseTerrain,
			GenerateTerrain,
			Exit
		};

		// One undo stack for the whole level. A scene entry is the document to
		// go back to; a terrain entry only names a transaction in the voxel
		// system's own history, which holds the voxels. One that history no
		// longer has (evicted, or the terrain was replaced) is skipped.
		struct UndoEntry
		{
			std::optional<SceneDocument>	scene;
			voxel::edit::History::Id		terrain{};
		};

		// Oldest entries are dropped past this many.
		static constexpr size_t kUndoLimit = 50;

		// ------------------------------ Core ------------------------------
		// EditorUI.cpp

		/*!
			@brief
				Adds a line to the Console; an error also opens and focuses the Console.
			@param level
				Severity of the message
			@param text
				The message
			*/
		void log(LogLevel level, std::string text);

		/*!
			@brief
				Runs an action, sending whatever it throws to the console.
			@param action
				The action to run
			*/
		void attempt(const std::function<void()>& action);

		/*!
			@brief
				Everything between ImGui::NewFrame and ImGui::Render.
			@param renderScene
				Draws the scene; handed to the Viewport panel
			*/
		void buildInterface(const std::function<engine::SceneView()>& renderScene);

		/*!
			@brief
				Rescans the asset folder and reloads the biome library.
			*/
		void refresh();
		/*!
			@brief
				Reads voxel/biomes.json from disk into biomeLibrary_, keeping the error when it fails.
			*/
		void loadBiomeLibrary();

		/*!
			@brief
				Decides how long the engine may wait before the next frame.

				A plain cap while anything is going on, a much longer one once the user has stopped
				touching the editor and nothing is running in the background. Called at the end of a
				frame, with that frame's input still in ImGui.
			*/
		void updateFramePacing();

		/*!
			@brief
				Tests whether the user did anything this frame, or is holding something down.
			@return
				True when there was any user input
			*/
		bool userActive() const;

		/*!
			@brief
				Copies the panel flags and the Performance choices into prefs_ and writes them if they
				changed.
			*/
		void storePrefs();

		// ------------------------------ Level -----------------------------
		// EditorUILevel.cpp

		/*!
			@brief
				Runs a command, after the unsaved-changes prompt when it would lose work.
			@param command
				The command to run
			@param target
				OpenLevel: the .voxscene to load. UseTerrain: the asset-relative .voxmap. Otherwise
				unused. GenerateTerrain takes its recipe from pendingRecipe_.
			*/
		void request(Command command, std::filesystem::path target = {});
		/*!
			@brief
				Carries out a command without asking.
			@param command
				The command to run
			*/
		void perform(Command command);

		/*!
			@brief
				Ctrl+S: writes the scene and, when it changed, the terrain. Asks for a name first if
				the level has none.
			*/
		void saveLevel();

		/*!
			@brief
				Writes the terrain (when asked) and the scene, then resumes the command waiting on the save.
			@param sceneFile
				Where to write the .voxscene
			@param terrainTarget
				Asset-relative .voxmap to write the voxel world to and reference from the scene; empty
				to leave the terrain alone
			*/
		void commitSave(const std::filesystem::path& sceneFile, const std::string& terrainTarget);
		/*!
			@brief
				Fills in the Save As popup and asks for it to open.
			@param folder
				Asset-relative folder to save into; empty picks a default
			*/
		void openSaveAs(const std::string& folder = {});

		/*!
			@brief
				Logs terrain progress changes and completes a save that was waiting on the terrain.
			*/
		void watchTerrain();

		/*!
			@brief
				Tests whether the level has unsaved changes.
			@return
				True when the scene is dirty or the terrain is modified
			*/
		bool		levelDirty() const;
		/*!
			@brief
				Returns the level's display name.
			@return
				The scene file's stem, "Untitled" or "No level"
			*/
		std::string	levelName() const;
		/*!
			@brief
				Returns the scene file's path relative to the asset root.
			@return
				The asset-relative path; empty when there is none
			*/
		std::string	levelRelative() const;

		/*!
			@brief
				Puts an entry on the undo stack, drops the oldest past kUndoLimit and clears redo.
			@param entry
				The scene snapshot or voxel transaction to record
			*/
		void pushUndo(UndoEntry entry);

		/*!
			@brief
				Records the scene as it is now, ahead of a change to it.
			*/
		void remember();
		/*!
			@brief
				Reverts the newest live entry of one stack and leaves its counterpart on the other.
			@param from
				Stack to take the entry from
			@param to
				Stack to put the counterpart on
			@param undoing
				True for undo, false for redo
			*/
		void stepHistory(std::vector<UndoEntry>& from, std::vector<UndoEntry>& to, bool undoing);
		/*!
			@brief
				Reverts the newest undo entry.
			*/
		void undo();
		/*!
			@brief
				Re-applies the newest redo entry.
			*/
		void redo();

		// ------------------------------ Chrome ----------------------------
		// EditorUIChrome.cpp

		/*!
			@brief
				Handles the global keyboard shortcuts.
			*/
		void shortcuts();

		/*!
			@brief
				Play, Pause, or Cancel while Play waits.

				Play does not start at once: the terrain's collision is built first, and toolbar()
				starts the simulation once voxel::System::collidersReady().
			*/
		void togglePlay();
		/*!
			@brief
				Draws the main menu bar.
			*/
		void menuBar();
		/*!
			@brief
				Draws the toolbar and starts a pending Play once the terrain's colliders are ready.
			*/
		void toolbar();
		/*!
			@brief
				Draws the status bar: level, terrain, last log line and frame rate.
			*/
		void statusBar();
		/*!
			@brief
				Draws the dockspace, building the default layout when there is none or on reset.
			*/
		void dockspace();

		// ------------------------------ Panels ----------------------------
		// EditorUIPanels.cpp

		/*!
			@brief
				Draws the Hierarchy panel.
			*/
		void hierarchy();
		/*!
			@brief
				Selects a scene object and loads it into the inspector's working copy.
			@param id
				Scene object ID; 0 selects nothing
			*/
		void select(uint32_t id);
		/*!
			@brief
				Draws the Inspector panel for the selected object.
			*/
		void inspector();
        bool wantsCameraInput() const override { return !gizmoDragging_; }

        unsigned inspectorEditId_{};
        bool showAssetReferences_{true};
        char audioSearch_[256]{};
        void gizmoToolbar();
        void gizmo(const engine::SceneView& view, float x, float y, float width, float height);
        void pickObject(const engine::SceneView& view, float x, float y, float width, float height);
        int gizmoOperation_{};
        bool gizmoAxisLocked_[3]{};
        bool editObjects_{true};
        bool showGizmoControls_{};
        bool gizmoLocal_{}, gizmoSnap_{}, gizmoDragging_{}, gizmoRecorded_{};
        float gizmoMoveSnap_{0.5f}, gizmoRotateSnap_{15.0f}, gizmoScaleSnap_{0.1f};
		/*!
			@brief
				Adds or removes a component on the selected object, as one undo entry.
			@param component
				Which component
			@param present
				True to add it, false to remove it
			*/
		void changeComponent(SceneComponent component, bool present);
		/*!
			@brief
				Draws the Content Browser panel and the previewed asset's details.
			*/
		void contentPanel();
		/*!
			@brief
				Loads an asset as the previewed one, unless it already is.
			@param relative
				Asset-relative path of the file
			*/
		void showPreview(const std::string& relative);
		/*!
			@brief
				Draws the Resources panel.
			*/
		void resourcePanel();
		/*!
			@brief
				Draws the Profiler panel.
			*/
		void profilerPanel();

		// ------------------------------ Terrain ---------------------------
		// EditorUITerrain.cpp

		/*!
			@brief
				Draws the Terrain panel, and adopts the settings of a terrain that has just arrived.
			*/
		void terrainPanel();

		/*!
			@brief
				Hot reload: notices the level's .voxgen (and biomes.json) being saved from another program.

				The pipeline preview always follows; the 3D terrain is regenerated at once unless it
				holds hand edits, which only the user may decide to discard.
			*/
		void watchRecipe();
		/*!
			@brief
				Reacts to the level's recipe changing on disk.
			@param file
				Absolute path of the changed .voxgen
			*/
		void recipeFileChanged(const std::filesystem::path& file);

		/*!
			@brief
				Returns the .voxgen beside the map the level names.
			@return
				Its path; empty when the level names no map
			*/
		std::filesystem::path levelRecipeFile() const;
		/*!
			@brief
				Draws the Generation Pipeline panel and drives its preview.
			*/
		void pipelinePanel();

		// ------------------------------ Viewport --------------------------
		// EditorUIViewport.cpp

		/*!
			@brief
				Draws the Viewport panel, rendering the scene into its own framebuffer.
			@param renderScene
				Draws the scene into whatever framebuffer is bound
			*/
		void viewport(const std::function<engine::SceneView()>& renderScene);

		/*!
			@brief
				Everything the editor draws over the scene and drives with the mouse there.

				The terrain region's box on the Create tab, the selected terrain tool on the Edit tab.
				Reads the pointer, then draws into the bound scene framebuffer and flushes the overlay
				once.
			@param imageX
				Screen x of the scene image's top-left corner
			@param imageY
				Screen y of the scene image's top-left corner
			@param imageWidth
				Width of the scene image on screen
			@param imageHeight
				Height of the scene image on screen
			*/
		void viewportTools(float imageX, float imageY, float imageWidth, float imageHeight);
		/*!
			@brief
				Updates and draws the terrain region gizmo, or releases it when it is not shown.
			@param input
				This frame's viewport pointer state
			*/
		void regionGizmo(const ViewportInput& input);
		/*!
			@brief
				Updates and draws the selected terrain tool, or deactivates the tools when unusable.
			@param input
				This frame's viewport pointer state
			*/
		void terrainTools(const ViewportInput& input);
		/*!
			@brief
				Builds what the terrain tools need from the editor.
			@return
				The tool context
			*/
		ToolContext toolContext();

		/*!
			@brief
				Moves the editor camera to where the whole terrain region is in view.
			*/
		void frameRegion();

		// ------------------------------ Dialogs ---------------------------
		// EditorUIDialogs.cpp

		/*!
			@brief
				Draws every modal popup the editor can ask for.
			*/
		void dialogs();
		/*!
			@brief
				Draws the unsaved-changes prompt.
			*/
		void unsavedPopup();
		/*!
			@brief
				Draws the Save level as popup.
			*/
		void saveAsPopup();
		/*!
			@brief
				Draws the Open level popup.
			*/
		void openLevelPopup();
		/*!
			@brief
				Draws the Choose terrain map popup.
			*/
		void chooseMapPopup();

		// ------------------------------ State -----------------------------
		// Declaration order is construction order, and destruction runs it
		// backwards. Keep it when adding members.

		// Engine services, handed over in onAttach.
		SceneManager*		scene_		{};
		ResourceManager*	resources_	{};
		events::Bus*		bus_		{};
		voxel::System*		voxel_		{};
		graphics::System*	graphics_	{};
		GLFWwindow*			window_		{};

		// Which parts of ImGui came up, so onDetach only tears those down.
		bool	contextReady_	{};
		bool	platformReady_	{};
		bool	rendererReady_	{};
		int		frames_			{};

		// The selected object and the inspector's working copy of it.
		uint32_t	selected_			{};
		SceneObject	draft_;
		glm::vec3	rotationDegrees_	{};
		char		name_[1024]			{};
		char		model_[1024]		{};
		char		texture_[1024]		{};
		char		audioPath_[1024]	{};
        char fontPath_[1024]{};
        char textContent_[4097]{};

		std::vector<UndoEntry>	undo_;
		std::vector<UndoEntry>	redo_;

		ResourceHandle	preview_;
		ContentBrowser	browser_;
		LogConsole		log_;
		ProfilerPanel	profiler_;
		RecentFiles		recent_;

		// Terrain panel, the region gizmo it shares its box with, and what
		// draws the gizmo.
		TerrainPanel	terrain_;
		TerrainToolset	tools_;		// the panel's Edit tab
		RegionGizmo		gizmo_;
		std::unique_ptr<graphics::renderer::OverlayRenderer>	overlay_;
		std::optional<voxel::BiomeLibrary>						biomeLibrary_;
		std::string												biomeLibraryError_;
		std::optional<voxel::Recipe>							pendingRecipe_;		// what Command::GenerateTerrain generates

		// The Generation Pipeline panel and what feeds it. previewRecipe_ is
		// whichever recipe changed last: the level's own, the Terrain panel's
		// choices, or a file saved elsewhere.
		PipelinePanel					pipeline_;
		PipelinePreview					pipelinePreview_;
		std::optional<voxel::Recipe>	previewRecipe_;
		std::string						previewSource_;
		bool							pipelineDirty_			{};			// a finished preview the panel has not drawn yet
		unsigned						previewedPanelVersion_	{};
		double							panelChangedAt_			{ -1. };	// when the panel's recipe last changed; < 0 when that has been taken up
		FileWatcher						recipeWatcher_;
		FileWatcher						biomeWatcher_;

		// A recipe saved on disk that the terrain does not show, because it
		// has hand edits.
		std::optional<voxel::Recipe>	reloadRecipe_;

		// A terrain arrived; the panel takes its settings from it on the next
		// frame, once the voxel system has finished installing it.
		bool							adoptTerrain_			{};
		::events::ScopedSubscription	worldLoaded_;

		// Level commands. pending_ waits on the unsaved prompt; afterSave_
		// waits on a save that the prompt's "Save" started and that may itself
		// need the Save As popup.
		Command					pending_			{ Command::None };
		Command					afterSave_			{ Command::None };
		std::filesystem::path	pendingPath_;
		bool					confirm_			{};
		bool					askSaveAs_			{};
		bool					askOpenLevel_		{};
		bool					askChooseMap_		{};
		bool					newLevelThenSaveAs_	{};
		std::string				newLevelFolder_;

		// Save As popup.
		char		saveName_[128]		{};
		std::string	saveFolder_;
		bool		saveOwnTerrain_		{ true };

		// Open Level / Choose Map popups.
		char		openFilter_[128]	{};
		std::string	openPick_;
		std::string	mapPick_;

		// A save that is waiting for the terrain to finish building.
		bool					saveWhenReady_	{};
		std::filesystem::path	deferredScene_;
		std::string				deferredTerrain_;

		// Terrain progress seen last frame, to log the changes.
		std::string	terrainStage_;
		std::string	terrainError_;

		// Input ownership, sampled at the end of each frame for the next
		// input update.
		bool	uiWantsKeyboard_	{};
		bool	viewportHovered_	{};
		bool	viewportDrag_		{};
		bool	gizmoHot_			{};

		// A terrain tool is in hand, so the viewport's left button and the
		// adjust key are its.
		bool	toolsActive_		{};

		bool	focusConsole_		{};
		bool	focusProfiler_		{};

		// Counts down to the frame that brings the Content Browser's tab
		// forward: at startup, and again after the layout is rebuilt.
		int		focusContentFrames_	{ 2 };

		std::string		title_;
		unsigned int	viewportFramebuffer_	{};
		unsigned int	viewportTexture_		{};
		unsigned int	viewportDepth_			{};
		int				viewportWidth_			{};
		int				viewportHeight_			{};

		bool		persistLayout_	{ true };
		bool		resetLayout_	{};
		bool		showHierarchy_	{ true };
		bool		showInspector_	{ true };
		bool		showAssets_		{ true };
		bool		showResources_	{ true };
		bool		showViewport_	{ true };
		bool		showConsole_	{ true };
		bool		showProfiler_	{ true };
		bool		showTerrain_	{ true };
		bool		showPipeline_	{ true };		// docked behind the Content Browser; runs its prepass only while in view
		bool		playing_		{ false };

		// Play was pressed and the terrain's collision meshes, which the editor
		// does not cook while paused, are being built; playing_ follows.
		bool		pendingPlay_	{ false };
		std::string	layoutFile_;

		// Window > Performance, kept in prefs_ between sessions. The cap holds
		// the editor to kActiveInterval; the idle throttle drops it further
		// once nothing has happened for a moment.
		EditorPrefs	prefs_;
		bool		limitFrameRate_	{ true };
		bool		idleThrottle_	{ true };
		double		frameInterval_	{ 0. };
		double		lastActivity_	{ 0. };		// ImGui time of the last frame that was not idle

		// The engine input system owns GLFW's user pointer, so the close
		// callback finds its editor here instead.
		static inline std::unordered_map<GLFWwindow*, EditorUI*> windows_;
	};
}
