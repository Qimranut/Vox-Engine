/*************************************************************************************************************
\file       EditorUITerrain.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      EditorUI terrain: the Terrain panel's context and actions, adopting a newly installed terrain,
            recipe and biome-library hot reload, and the Generation Pipeline panel and what feeds its preview.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "EditorUI.hpp"

// Editor
#include "EditorUICommon.hpp"
#include "ShellOpen.hpp"

// Voxel
#include "voxel/sys/voxel_sys.hpp"

// ImGui
#include <imgui.h>

// stds
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>


namespace vox::editor
{
	/*!
		@brief
			Draws the Terrain panel for the level's voxel terrain: its file, generation and the
			renderer's debug view.

			Saving and opening it belongs to the level (File menu); generation runs on the engine's
			worker threads and this panel only polls it. Also adopts the settings of a terrain that
			has just been installed, even while the panel is closed.
		*/
	void EditorUI::terrainPanel()
	{
		// A terrain that was just installed says what made it; the panel opens on that.
		if (adoptTerrain_ && !gizmo_.dragging())
		{
			adoptTerrain_ = false;
			const voxel::BiomeLibrary* library = biomeLibrary_ ? &*biomeLibrary_ : nullptr;
			if (const voxel::Recipe* recipe = voxel_->recipe())
			{
				terrain_.adopt(*recipe, library);

				// A streamed terrain is centred on its origin and exists only round the camera,
				// which starts out at the origin too - under the ground, looking at nothing, and
				// with nothing to look for. Put it where the terrain's middle is in view.
				if (recipe->stream_.endless_)
					frameRegion();
			}
			else if (voxel_->hasWorld())
				terrain_.adopt(voxel_->origin(), voxel_->worldSizeVoxels(), voxel_->voxelSize());

			// The pipeline view shows how this terrain came to be. One read from a map with no
			// recipe beside it has no pipeline to show.
			if (const voxel::Recipe* recipe = voxel_->recipe())
			{
				previewRecipe_ = *recipe;
				previewSource_ = scene_->world().empty()
				                     ? "the level's terrain (not saved yet)"
				                     : "the level's terrain, " + levelRecipeFile().filename().string();
			}

			// Whatever was waiting to be reloaded is either what just arrived or out of date.
			reloadRecipe_.reset();
			panelChangedAt_ = -1.;
			previewedPanelVersion_ = terrain_.userVersion();
		}

		if (!showTerrain_)
			return;

		if (ImGui::Begin("Terrain", &showTerrain_))
		{
			const voxel::Progress progress = voxel_->progress();

			// ------------------------------ Context ------------------------------

			TerrainPanel::Context context;
			context.library = biomeLibrary_ ? &*biomeLibrary_ : nullptr;
			context.libraryError = biomeLibraryError_;
			context.levelLoaded = scene_->loaded();
			context.busy = progress.busy_;
			context.progress = progress.fraction_;
			context.stage = progress.stage_;
			context.gizmoDragging = gizmo_.dragging();
			context.mapFile = scene_->world();
			context.hasTerrain = voxel_->hasWorld();
			context.terrainReady = voxel_->ready();
			context.terrainModified = voxel_->modified();

			if (context.hasTerrain)
			{
				context.terrainVoxels = voxel_->worldSizeVoxels();
				context.terrainVoxelSize = voxel_->voxelSize();
			}

			context.render = &voxel_->renderSettings();
			context.stats = &voxel_->stats();
			context.drawEditTab = [this]
			{
				ToolContext tool = toolContext();
				const bool running = playing_ || pendingPlay_;
				const char* unavailable =
					!scene_->loaded()     ? "Open or create a level first: terrain belongs to a level."
					: running             ? "Pause to edit the terrain."
					: !voxel_->hasWorld() ? "This level has no terrain yet. Generate one on the Create tab, or choose a map."
					: !voxel_->editable() ? "The terrain is being built..."
					:                       nullptr;
				tools_.drawPanel(tool, unavailable);
			};

			// ------------------------------ Actions -------------------------------

			TerrainPanel::Actions actions;
			actions.generate = [this](const voxel::Recipe& recipe)
			{
				pendingRecipe_ = recipe;
				request(Command::GenerateTerrain);
			};
			actions.cancel = [this]
			{
				voxel_->cancel();
				log(LogLevel::Warn, "Terrain generation cancelled.");
			};
			actions.frame = [this]
			{
				frameRegion();
			};
			actions.chooseMap = [this]
			{
				askChooseMap_ = true;
			};
			actions.revertTerrain = [this]
			{
				attempt(
					[&]
					{
						const std::string world = scene_->world();
						voxel_->syncWorld(world);
						log(LogLevel::Info, "Reloading " + world + " from disk.");
					});
			};
			actions.removeTerrain = [this]
			{
				attempt(
					[&]
					{
						if (!scene_->world().empty())
						{
							remember();
							scene_->setWorld("");
						}
						voxel_->syncWorld("");
						log(LogLevel::Info, "Terrain removed from the level.");
					});
			};

			terrain_.draw(context, actions);
		}

		ImGui::End();
	}

	/*!
		@brief
			Returns the .voxgen beside the map the level names.
		@return
			Its absolute path; empty when the level names no map
		*/
	std::filesystem::path EditorUI::levelRecipeFile() const
	{
		if (scene_->world().empty())
			return {};

		std::filesystem::path file = resources_->resolve(scene_->world());
		file.replace_extension(".voxgen");
		return file;
	}

	/*!
		@brief
			Hot reload: notices the level's .voxgen and biomes.json being saved from another program,
			and hands the Terrain panel's settings to the pipeline preview once they stop changing.

			The pipeline preview always follows a recipe change; the 3D terrain is regenerated at once
			unless it holds hand edits, which only the user may decide to discard.
		*/
	void EditorUI::watchRecipe()
	{
		const double now = ImGui::GetTime();

		recipeWatcher_.watch(scene_->loaded() ? levelRecipeFile() : std::filesystem::path{});
		if (recipeWatcher_.poll(now))
			recipeFileChanged(recipeWatcher_.file());

		// Biomes are data too: a biome tuned in a text editor shows up in the panel, and through
		// the panel's recipe in the pipeline view, without a Refresh.
		biomeWatcher_.watch(resources_->root() / "voxel/biomes.json");
		if (biomeWatcher_.poll(now))
		{
			loadBiomeLibrary();
			log(biomeLibrary_ ? LogLevel::Info : LogLevel::Error,
			    biomeLibrary_ ? "voxel/biomes.json changed on disk; the biome library was reloaded."
			                  : "voxel/biomes.json: " + biomeLibraryError_);
		}

		// The Terrain panel's own choices, once the user has stopped changing them for a moment.
		if (terrain_.userVersion() != previewedPanelVersion_)
		{
			previewedPanelVersion_ = terrain_.userVersion();
			panelChangedAt_ = now;
		}

		if (panelChangedAt_ >= 0. && now - panelChangedAt_ > 0.35 && !gizmo_.dragging())
		{
			panelChangedAt_ = -1.;
			if (terrain_.composed())
			{
				previewRecipe_ = *terrain_.composed();
				previewSource_ = "the Terrain panel's settings (not generated yet)";
			}
		}
	}

	/*!
		@brief
			Reacts to the level's recipe changing on disk: previews it, and regenerates the terrain
			from it unless the terrain has hand edits, in which case the pipeline panel offers the choice.

			A recipe that does not parse is logged and ignored; one equal to the current recipe is ignored.
		@param file
			Absolute path of the changed .voxgen
		*/
	void EditorUI::recipeFileChanged(const std::filesystem::path& file)
	{
		const std::string relative = resources_->relative(file);
		std::error_code ec;
		if (!std::filesystem::exists(file, ec))
			return; // deleted: the terrain in memory still has its recipe

		voxel::Recipe recipe;
		try
		{
			const auto bytes = readFileBytes(file);
			if (!bytes)
				throw std::runtime_error("could not be read");

			recipe = voxel::Recipe::parse(*bytes);
		}
		catch (const std::exception& e)
		{
			// The terrain and the preview keep the last recipe that parsed.
			log(LogLevel::Error, relative + ": " + e.what());
			return;
		}

		// The editor's own save, or an edit that only moved whitespace.
		if (const voxel::Recipe* current = voxel_->recipe(); current && *current == recipe)
			return;

		previewRecipe_ = recipe;
		previewSource_ = relative + " (changed on disk)";

		if (voxel_->edited())
		{
			// The panel holds the choice between the recipe and the hand edits, so it opens here
			// and only here: otherwise it stays closed, and costs nothing.
			showPipeline_ = true;
			reloadRecipe_ = std::move(recipe);
			log(LogLevel::Warn,
			    relative
			        + " changed on disk. The pipeline view shows it; the terrain has hand edits, so it was "
			          "not regenerated (see the Generation Pipeline panel).");
			return;
		}

		// Straight to the voxel system, not through Command::GenerateTerrain: that is for a new
		// terrain of the level's own, and would let go of the map this recipe sits beside.
		ToolContext tool = toolContext();
		tools_.deactivate(tool);
		voxel_->generate(std::move(recipe), scene_->world());
		log(LogLevel::Info, relative + " changed on disk; regenerating the terrain from it.");
	}

	/*!
		@brief
			Draws the Generation Pipeline panel and drives its preview: requests the prepass of the
			latest recipe while the panel is in view (one tile's worth for an endless recipe) and
			abandons it while it is not.
		*/
	void EditorUI::pipelinePanel()
	{
		if (!showPipeline_)
		{
			pipelinePreview_.abandon();
			return;
		}

		const bool visible = ImGui::Begin("Generation Pipeline", &showPipeline_);

		// The prepass only runs for someone to look at it: closed, collapsed or behind another
		// tab, a run in progress is given up and starts again when the panel is next in view.
		if (visible && previewRecipe_)
		{
			// A streamed terrain is never made whole, so there is no whole to show: the panel shows
			// the prepass of one tile's worth from its middle, as a bounded map. (A real tile also
			// fades its erosion out towards its border; this does not.)
			if (previewRecipe_->stream_.endless_)
			{
				voxel::Recipe window = *previewRecipe_;
				const float side = static_cast<float>(window.stream_.tileCells_ * 256) * window.voxelSize_;
				const glm::vec3 centre = window.origin_ + 0.5f * glm::vec3(window.size_) * window.voxelSize_;

				window.stream_ = {};
				window.size_.x = window.size_.z = static_cast<int>(std::lround(side / window.voxelSize_));
				window.origin_.x = centre.x - 0.5f * side;
				window.origin_.z = centre.z - 0.5f * side;
				pipelinePreview_.request(window, voxel_->materials());
			}
			else
				pipelinePreview_.request(*previewRecipe_, voxel_->materials());
		}
		else if (!visible)
			pipelinePreview_.abandon();

		// A result that arrives while the panel is hidden still has to reach its pictures later.
		if (pipelinePreview_.poll())
			pipelineDirty_ = true;

		if (visible)
		{
			// ------------------------------ Context ------------------------------

			PipelinePanel::Context context;
			context.resultChanged = pipelineDirty_;
			pipelineDirty_ = false;
			context.result = pipelinePreview_.result();
			context.busy = pipelinePreview_.busy();
			context.progress = pipelinePreview_.progress();
			context.materials = &voxel_->materials();
			context.source = previewSource_;
			context.reloadPending = reloadRecipe_.has_value();

			const std::filesystem::path file = levelRecipeFile();
			std::error_code ec;
			if (!file.empty() && std::filesystem::exists(file, ec))
				context.recipeFile = resources_->relative(file);

			// ------------------------------ Actions -------------------------------

			PipelinePanel::Actions actions;
			actions.openRecipeFile = [this]
			{
				const std::filesystem::path file = levelRecipeFile();
				if (!openInTextEditor(file))
					log(LogLevel::Error, "Could not open " + file.string() + " in a text editor.");
			};
			actions.regenerate = [this]
			{
				if (!reloadRecipe_)
					return;
				ToolContext tool = toolContext();
				tools_.deactivate(tool);
				voxel_->generate(*reloadRecipe_, scene_->world());
				reloadRecipe_.reset();
				log(LogLevel::Warn,
				    "Regenerating the terrain from the recipe on disk; its hand edits are gone.");
			};
			actions.dismissReload = [this]
			{
				reloadRecipe_.reset();
				if (const voxel::Recipe* recipe = voxel_->recipe())
				{
					previewRecipe_ = *recipe;
					previewSource_ = "the level's terrain";
				}
			};

			pipeline_.draw(context, actions);
		}

		ImGui::End();
	}
}
