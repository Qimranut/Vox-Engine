/*************************************************************************************************************
\file       TerrainPanel.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the Editor's Terrain panel: the level's terrain file, a Create tab that composes a
            biome recipe from the user's choices and the region box, an Edit tab for the terrain tools,
            and the voxel renderer's debug switches and stats.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Editor
#include "gizmo/RegionGizmo.hpp"

// Engine
#include "voxel/gen/biome/biome_library.hpp"
#include "voxel/gen/recipe.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <functional>
#include <optional>
#include <string>
#include <vector>


namespace voxel
{
	struct RenderSettings;
	struct Stats;
}

namespace vox::editor
{
	/**
	 * The Terrain panel: the level's terrain file; a Create tab with where terrain goes, which
	 * biomes it mixes and the Generate button; an Edit tab with the terrain tools, which the
	 * editor draws (see terrain/TerrainToolset); and the voxel renderer's debug switches and numbers.
	 *
	 * It owns the user's choices and turns them into a recipe with voxel::composeBiomeRecipe,
	 * the same function VoxelProbe and the engine's self-check use, so what the panel generates
	 * is exactly what a saved .voxgen regenerates. The region is shared with the viewport gizmo:
	 * typing numbers here moves the box, dragging the box changes the numbers.
	 *
	 * Like the other panels it draws only its body and reports what the user asked for through
	 * Actions; it never touches the voxel system or the level itself. The one exception is the
	 * render settings it is handed, which are plain switches it edits in place.
	 *
	 */
	class TerrainPanel
	{
	public:

		struct Actions
		{
			std::function<void(const voxel::Recipe&)>	generate;
			std::function<void()>						cancel;
			std::function<void()>						frame;			// bring the whole region into view
			std::function<void()>						chooseMap;		// open the Choose Map popup
			std::function<void()>						revertTerrain;	// reload the saved map, dropping generated or edited terrain
			std::function<void()>						removeTerrain;	// the level has no terrain any more
		};

		/** What the panel needs to know about the editor this frame. */
		struct Context
		{
			const voxel::BiomeLibrary*	library{};			// null when biomes.json did not load
			std::string					libraryError;		// why, in that case
			bool						levelLoaded{};
			bool						busy{};				// terrain is being generated or loaded
			float						progress{};			// 0..1 while busy
			std::string					stage;
			bool						gizmoDragging{};

			// The level's terrain as it stands.
			std::string					mapFile;			// the map the scene names; empty when it names none
			bool						hasTerrain{};
			bool						terrainReady{};
			bool						terrainModified{};
			glm::ivec3					terrainVoxels{};	// size of the terrain that is loaded, in voxels
			float						terrainVoxelSize{};
			voxel::RenderSettings*		render{};			// edited in place; null hides the section
			const voxel::Stats*		stats{};			// null hides the section

			// Draws the body of the Edit tab: the terrain tools, which the editor owns because
			// they reach the voxel system and the viewport. Null leaves the tab out.
			std::function<void()>		drawEditTab;
		};

		/*!
			@brief
				Draws the panel body; call between ImGui::Begin and End.
			@param context
				What the panel needs to know about the editor this frame
			@param actions
				Where the panel sends what the user asked for
			*/
		void draw(const Context& context, const Actions& actions);

		/*!
			@brief
				The region box shared with the viewport gizmo, for editing.
			@return
				The region
			*/
		TerrainRegion&			region()		{ return region_; }

		/*!
			@brief
				The region box shared with the viewport gizmo.
			@return
				The region
			*/
		const TerrainRegion&	region() const	{ return region_; }

		/*!
			@brief
				Tells the panel the region changed outside it (the gizmo), so the recipe is recomposed.
			*/
		void regionMoved() { stale_ = true; }

		/*!
			@brief
				Whether the region box should show in the viewport.

				The box belongs to the Create tab; the Edit tab's tools own the viewport instead.
			@return
				True when Show region is ticked and the Edit tab is not showing
			*/
		bool showRegion() const { return showRegion_ && !editing_; }

		/*!
			@brief
				Whether the Edit tab is the one showing.
			@return
				True while the Edit tab is showing
			*/
		bool editing() const { return editing_; }

		/*!
			@brief
				The recipe Generate would make right now.
			@return
				The composed recipe; empty while the choices do not compose
			*/
		const std::optional<voxel::Recipe>& composed() const { return composed_; }

		/*!
			@brief
				Counts the times the user's own changes recomposed the recipe.

				Taking settings from a loaded terrain does not count: that recipe is the terrain's,
				not a new idea to preview.
			@return
				The count
			*/
		unsigned userVersion() const { return userVersion_; }

		/*!
			@brief
				Whether size and position snap to whole voxels.
			@return
				The Snap to Voxels setting
			*/
		bool	snapToVoxels() const	{ return snap_; }

		/*!
			@brief
				The voxel size the panel composes with, in metres.
			@return
				Voxel edge length in metres
			*/
		float	voxelSize() const		{ return voxelSize_; }

		/*!
			@brief
				Takes size, position and, for biome terrain, every biome setting from a recipe.

				The recipe is the one a loaded terrain was generated from, so the panel opens on what
				made the level. An endless recipe puts the box back to 102 m in the terrain's middle.
				Only biomes the library still has are ticked again.
			@param recipe
				The recipe the terrain was generated from
			@param library
				The biome library, or null to take every biome name as is
			*/
		void adopt(const voxel::Recipe& recipe, const voxel::BiomeLibrary* library);

		/*!
			@brief
				The same for terrain with no recipe: only where it is and how big.
			@param origin
				Lowest corner of the terrain, in metres
			@param sizeVoxels
				Size of the terrain in voxels
			@param voxelSize
				Voxel edge length in metres; 0.1 when not positive
			*/
		void adopt(const glm::vec3& origin, const glm::ivec3& sizeVoxels, float voxelSize);

	private:

		/*!
			@brief
				Gathers the panel's choices into the settings composeBiomeRecipe takes; an endless
				terrain uses the extent round the box's middle instead of the box.
			@param library
				The biome library (unused)
			@return
				The generate settings
			*/
		voxel::GenerateSettings settings(const voxel::BiomeLibrary& library) const;

		/*!
			@brief
				Composes the recipe from the current choices, keeping the error text when it fails,
				and counts the change as the user's unless it was a quiet catch-up.
			@param library
				The biome library to compose against
			*/
		void recompose(const voxel::BiomeLibrary& library);

		/*!
			@brief
				Draws the Create tab: selection settings, biome settings, the composer's problem or
				warning, the Generate button and progress.
			@param context
				What the panel needs to know about the editor this frame
			@param actions
				Where the panel sends what the user asked for
			*/
		void drawCreateTab(const Context& context, const Actions& actions);

		TerrainRegion				region_;
		bool						editing_		{};
		float						voxelSize_		{ 0.1f };
		bool						snap_			{ true };
		bool						showRegion_		{ true };
		std::vector<std::string>	chosen_			{ "hills", "plains" }; // biome names, so a reloaded library keeps the ticks
		float						blending_		{ 0.3f };
		float						biomeSize_		{ 40.f };

		// A streamed terrain: the box then only says how tall the terrain is
		// and where its middle is, and endlessExtent_ (metres a side) how far
		// the player may roam.
		bool						endless_		{ false };
		float						endlessExtent_	{ 1638.4f };
		unsigned long long			seed_			{ 618033988 };

		// The recipe these choices compose to, or why they do not; rebuilt when stale_.
		std::optional<voxel::Recipe>	composed_;
		std::string						problem_;
		bool							stale_			{ true };
		const voxel::BiomeLibrary*		composedFrom_	{};
		// The next recompose is the panel catching up (start-up, adopt()), not the user's doing.
		bool							quiet_			{ true };
		unsigned						userVersion_	{};
	};
}
