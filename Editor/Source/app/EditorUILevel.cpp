/*************************************************************************************************************
\file       EditorUILevel.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      EditorUI level handling: level commands behind the unsaved-changes prompt (new, open, close,
            use / generate terrain, exit), saving the scene with its terrain (deferred while the terrain is
            busy), and the level-wide undo / redo stack over scene snapshots and voxel transactions.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "EditorUI.hpp"

// Editor
#include "EditorUICommon.hpp"

// Voxel
#include "voxel/sys/voxel_sys.hpp"

// GLFW
#include <GLFW/glfw3.h>

// stds
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>


namespace vox::editor
{
	/*!
		@brief
			Tests whether the level has unsaved changes.
		@return
			True when the scene is dirty or the terrain is modified
		*/
	bool EditorUI::levelDirty() const
	{
		return scene_->dirty() || voxel_->modified();
	}

	/*!
		@brief
			Returns the level's display name.
		@return
			The scene file's stem, "Untitled" for an unsaved level, or "No level"
		*/
	std::string EditorUI::levelName() const
	{
		if (!scene_->loaded())
			return "No level";

		return scene_->path().empty() ? "Untitled" : scene_->path().stem().string();
	}

	/*!
		@brief
			Returns the scene file's path relative to the asset root.
		@return
			The asset-relative path; empty when the level has no file or it lies outside the root
		*/
	std::string EditorUI::levelRelative() const
	{
		if (scene_->path().empty())
			return {};

		try
		{
			return resources_->relative(scene_->path());
		}
		catch (const std::exception&)
		{
			return {};
		}
	}

	/*!
		@brief
			Runs a command, after the unsaved-changes prompt when it would lose work.

			UseTerrain only asks when the terrain is modified, GenerateTerrain only when it has hand
			edits, everything else when the level is dirty.
		@param command
			The command to run
		@param target
			OpenLevel: the .voxscene to load. UseTerrain: the asset-relative .voxmap. Otherwise unused.
			GenerateTerrain takes its recipe from pendingRecipe_.
		*/
	void EditorUI::request(Command command, std::filesystem::path target)
	{
		pending_ = command;
		pendingPath_ = std::move(target);

		// Swapping the terrain keeps the scene, so only unsaved terrain is at stake there.
		// Generating replaces terrain that a recipe can make again; only hand edits are lost for good.
		const bool losesWork =
			command == Command::UseTerrain			? voxel_->modified()
			: command == Command::GenerateTerrain	? voxel_->edited()
			: levelDirty();

		if (losesWork)
			confirm_ = true;
		else
			attempt([&] { perform(command); });
	}

	/*!
		@brief
			Carries out a command without asking, using pendingPath_ and pendingRecipe_.

			Resets selection, undo and preview when the level is replaced, and re-syncs the voxel
			system by hand when the scene's map path did not change, which discards unsaved terrain.
			Throws when the command cannot run (no level loaded, level file missing).
		@param command
			The command to run
		*/
	void EditorUI::perform(Command command)
	{
		pending_ = Command::None;

		if (command == Command::None)
			return;

		// ---------------------------- Exit ----------------------------

		if (command == Command::Exit)
		{
			glfwSetWindowShouldClose(window_, GLFW_TRUE);
			return;
		}

		// ------------------------ Use terrain -------------------------

		if (command == Command::UseTerrain)
		{
			if (!scene_->loaded())
				throw std::runtime_error("Open or create a level before choosing its terrain.");

			const std::string map = pendingPath_.generic_string();
			const bool sameFile = scene_->world() == map;

			remember();
			scene_->setWorld(map);

			// SceneManager only announces a change; the same file again means "reload it".
			if (sameFile)
				voxel_->syncWorld(map);

			log(LogLevel::Info, "Terrain set to " + map + ". Save the level to keep it.");
			return;
		}

		// ---------------------- Generate terrain ----------------------

		if (command == Command::GenerateTerrain)
		{
			if (!scene_->loaded())
				throw std::runtime_error("Open or create a level before generating its terrain.");

			if (!pendingRecipe_)
				return;

			// Generated terrain is the level's own. A level that borrowed another map (a shared
			// arena, a hand-tuned recipe) lets go of it, so saving cannot overwrite that map or
			// the recipe beside it; the level's own map is named when it is saved.
			const std::string own = "maps/" + levelName() + ".voxmap";
			if (!scene_->world().empty() && scene_->world() != own)
			{
				log(LogLevel::Info,
					"The level no longer uses " + scene_->world() + "; its new terrain will be saved as "
						+ own + ".");
				remember();
				scene_->setWorld("");
			}

			std::string biomes;
			for (const auto& biome : pendingRecipe_->biomes_.entries_)
				biomes += (biomes.empty() ? "" : ", ") + biome.name_;

			voxel_->generate(*pendingRecipe_, "generated terrain");

			if (biomes.empty())
				biomes = "recipe file";

			log(LogLevel::Info,
				"Generating terrain (" + biomes + ", seed " + std::to_string(pendingRecipe_->seed_) + ")...");
			pendingRecipe_.reset();
			return;
		}

		// --------------------- New / Open / Close ---------------------

		// Unsaved terrain is discarded along with the scene. SceneManager tells the voxel system
		// about a different map itself; the same map (or none, twice) has to be re-synced by hand.
		const std::string worldBefore = scene_->world();
		const bool discardTerrain = voxel_->modified();

		if (command == Command::NewLevel)
			scene_->newScene();

		if (command == Command::OpenLevel)
		{
			const std::string relative = pendingPath_.lexically_relative(resources_->root()).generic_string();

			std::error_code ec;
			if (!std::filesystem::exists(pendingPath_, ec))
			{
				recent_.remove(relative);
				throw std::runtime_error("Level not found: " + relative);
			}

			scene_->load(pendingPath_);
			recent_.push(levelRelative());
		}

		if (command == Command::CloseLevel)
			scene_->unload();

		selected_ = 0;
		undo_.clear();
		redo_.clear();
		preview_.reset();
		resources_->collectUnused();

		if (discardTerrain && scene_->world() == worldBefore)
			voxel_->syncWorld(worldBefore);

		if (command == Command::NewLevel)
			log(LogLevel::Info, "New level. Save it (Ctrl+S) to give it a name.");

		if (command == Command::OpenLevel)
			log(LogLevel::Info,
				"Opened " + levelRelative()
					+ (scene_->world().empty() ? "." : " with terrain " + scene_->world() + "."));

		if (command == Command::CloseLevel)
			log(LogLevel::Info, "Level closed.");

		if (command == Command::NewLevel && newLevelThenSaveAs_)
			openSaveAs(newLevelFolder_);

		newLevelThenSaveAs_ = false;
	}

	/*!
		@brief
			Ctrl+S: writes the scene and, when it changed, the terrain. Asks for a name first if the
			level has none.
		*/
	void EditorUI::saveLevel()
	{
		if (!scene_->loaded())
			return;

		if (scene_->path().empty())
		{
			openSaveAs();
			return;
		}

		// Terrain that was generated or edited goes to the map the scene names, or to one named
		// after the level when it has none yet.
		std::string terrain;
		if (voxel_->modified())
			terrain = scene_->world().empty() ? "maps/" + levelName() + ".voxmap" : scene_->world();

		commitSave(scene_->path(), terrain);
	}

	/*!
		@brief
			Writes the terrain (when asked) and the scene, then resumes the command parked in afterSave_.

			When the terrain cannot be saved yet the save is deferred and watchTerrain() completes it.
		@param sceneFile
			Where to write the .voxscene
		@param terrainTarget
			Asset-relative .voxmap to write the voxel world to and reference from the scene; empty to
			leave the terrain alone
		*/
	void EditorUI::commitSave(const std::filesystem::path& sceneFile, const std::string& terrainTarget)
	{
		if (!terrainTarget.empty())
		{
			if (!voxel_->canSave())
			{
				// saveWorld refuses a world that is building or remeshing after an edit;
				// watchTerrain() finishes this save.
				saveWhenReady_ = true;
				deferredScene_ = sceneFile;
				deferredTerrain_ = terrainTarget;
				log(LogLevel::Warn,
					"The terrain is still building. The level will be saved as soon as it is ready.");
				return;
			}

			std::filesystem::create_directories(
				resources_->root() / std::filesystem::path(terrainTarget).parent_path());

			if (voxel_->endless())
			{
				// A streamed terrain is its recipe; there is no map to hold what a brush did.
				if (voxel_->edited())
					log(LogLevel::Warn,
						"This terrain is endless: it saves as its recipe alone, and the hand edits made to it last "
						"for this session only.");
			}
			else if (voxel_->edited() && voxel_->recipe())
				log(LogLevel::Warn,
					"This terrain has hand edits. They are saved in the map, but maps are not under version "
					"control: anyone regenerating it from its recipe will not see them unless the .voxmap is "
					"shared too.");

			voxel_->saveWorld(terrainTarget);
			scene_->setWorld(terrainTarget);
		}

		std::filesystem::create_directories(sceneFile.parent_path());
		scene_->save(sceneFile);
		recent_.push(levelRelative());
		refresh();

		log(LogLevel::Info,
			"Saved " + levelRelative()
				+ (terrainTarget.empty() ? "." : " and terrain " + terrainTarget + "."));

		// A save started from the unsaved-changes prompt carries on with what was asked.
		const Command next = afterSave_;
		afterSave_ = Command::None;

		if (next != Command::None)
			perform(next);
	}

	/*!
		@brief
			Fills in the Save As popup's name and folder and asks for it to open.
		@param folder
			Asset-relative folder to save into; empty picks the level's own folder, or "scenes"
		*/
	void EditorUI::openSaveAs(const std::string& folder)
	{
		const std::string current = levelRelative();
		copy(saveName_, current.empty() ? std::string("NewLevel") : scene_->path().stem().string());

		const auto& folders = browser_.folders();
		const bool haveScenes = std::find(folders.begin(), folders.end(), "scenes") != folders.end();

		if (!folder.empty())
			saveFolder_ = folder;
		else if (!current.empty())
			saveFolder_ = std::filesystem::path(current).parent_path().generic_string();
		else
			saveFolder_ = haveScenes ? "scenes" : "";

		saveOwnTerrain_ = true;
		askSaveAs_ = true;
	}

	/*!
		@brief
			Logs terrain progress changes and completes a save that was waiting on the terrain.
		*/
	void EditorUI::watchTerrain()
	{
		const voxel::Progress progress = voxel_->progress();

		if (progress.stage_ != terrainStage_ || progress.error_ != terrainError_)
		{
			if (progress.stage_ == "failed")
				log(LogLevel::Error, "Terrain " + progress.error_);
			else if (progress.stage_ == "ready")
				log(LogLevel::Info,
					voxel_->modified()	? "Terrain ready (not saved yet)."
										: "Terrain ready: " + voxel_->mapPath());

			terrainStage_ = progress.stage_;
			terrainError_ = progress.error_;
		}

		if (!saveWhenReady_)
			return;

		// Nothing left to wait for: the load or generate failed, or the terrain was removed.
		if (!voxel_->hasWorld() && !progress.busy_)
		{
			saveWhenReady_ = false;
			afterSave_ = Command::None;
			log(LogLevel::Error, "The level was not saved: the terrain it was waiting for never finished.");
			return;
		}

		// Not progress.busy_: remeshing after an edit is not a load, and does not report as one.
		if (!voxel_->canSave())
			return;

		saveWhenReady_ = false;
		attempt([&] { commitSave(deferredScene_, deferredTerrain_); });
	}

	/*!
		@brief
			Puts an entry on the undo stack, drops the oldest past kUndoLimit and clears redo.
		@param entry
			The scene snapshot or voxel transaction to record
		*/
	void EditorUI::pushUndo(UndoEntry entry)
	{
		undo_.push_back(std::move(entry));

		if (undo_.size() > kUndoLimit)
			undo_.erase(undo_.begin());

		// Whatever was undone before this change can no longer follow it.
		redo_.clear();
	}

	/*!
		@brief
			Records the scene as it is now, ahead of a change to it.
		*/
	void EditorUI::remember()
	{
		pushUndo({ scene_->snapshot(), 0 });
	}

	/*!
		@brief
			Reverts the newest live entry of one stack and leaves its counterpart on the other.

			Undo and redo are the same walk in opposite directions. Terrain entries the voxel history
			no longer holds are skipped, and nothing happens while a terrain tool is busy.
		@param from
			Stack to take the entry from
		@param to
			Stack to put the counterpart on
		@param undoing
			True for undo, false for redo
		*/
	void EditorUI::stepHistory(std::vector<UndoEntry>& from, std::vector<UndoEntry>& to, bool undoing)
	{
		// A stroke still being drawn is not on the stack yet; undoing under it would reorder history.
		if (tools_.busy() || gizmoDragging_)
			return;

		while (!from.empty())
		{
			UndoEntry entry = std::move(from.back());
			from.pop_back();

			if (entry.scene)
			{
				attempt(
					[&]
					{
						auto current = scene_->snapshot();
						scene_->replace(*entry.scene);
						to.push_back({ std::move(current), 0 });
						selected_ = 0;
					});
				return;
			}

			// The voxels are in the voxel system's history; a transaction it no longer holds
			// (evicted for space, or the terrain was replaced) has nothing to revert.
			if (!voxel_->editAlive(entry.terrain))
				continue;

			if (undoing)
				voxel_->undoEdit();
			else
				voxel_->redoEdit();

			to.push_back(entry);
			return;
		}
	}

	/*!
		@brief
			Reverts the newest undo entry.
		*/
	void EditorUI::undo()
	{
		stepHistory(undo_, redo_, true);
	}

	/*!
		@brief
			Re-applies the newest redo entry.
		*/
	void EditorUI::redo()
	{
		stepHistory(redo_, undo_, false);
	}
}
