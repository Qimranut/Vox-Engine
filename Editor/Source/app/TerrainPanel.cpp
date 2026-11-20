/*************************************************************************************************************
\file       TerrainPanel.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements the Terrain panel: level terrain actions, the Create and Edit tabs, recipe
            composition through voxel::composeBiomeRecipe, and the rendering and stats sections.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "TerrainPanel.hpp"

// Engine
#include "voxel/sys/voxel_sys.hpp"

// ImGui
#include <imgui.h>

// stds
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <random>


namespace vox::editor
{
	namespace
	{
		using Limits = voxel::GenerateLimits;

		// Text colour for a non-fatal warning, e.g. relief squeezed by a low box.
		const ImVec4 kWarningText(1.00f, 0.80f, 0.35f, 1);

		// Text colour for a blocking problem, e.g. a recipe that will not compose.
		const ImVec4 kErrorText(1.00f, 0.45f, 0.40f, 1);

		/*!
			@brief
				Capitalises the first letter of a biome name for display.
			@param name
				The name to capitalise
			@return
				The name with its first letter upper-case
			*/
		std::string titled(std::string name)
		{
			if (!name.empty())
				name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));

			return name;
		}

		/*!
			@brief
				Shows a tooltip for the last item after a short hover, even when it is disabled.
			@param text
				The tooltip text
			*/
		void tooltip(const char* text)
		{
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("%s", text);
		}
	}

	/*!
		@brief
			Gathers the panel's choices into the settings composeBiomeRecipe takes; an endless
			terrain uses the extent round the box's middle instead of the box.
		@param const voxel::BiomeLibrary&
			The biome library (unused)
		@return
			The generate settings
		*/
	voxel::GenerateSettings TerrainPanel::settings(const voxel::BiomeLibrary&) const
	{
		voxel::GenerateSettings s;
		s.voxelSize_ = voxelSize_;
		s.size_ = glm::ivec3(glm::round(region_.size_ / voxelSize_));
		s.origin_ = region_.min_;
		s.biomes_ = chosen_;
		s.blending_ = blending_;
		s.biomeSize_ = biomeSize_;
		s.seed_ = seed_;

		// Streamed: the extent round the box's middle, instead of the box.
		if (endless_)
		{
			const glm::vec3 centre = region_.min_ + 0.5f * region_.size_;
			const int extent = static_cast<int>(std::lround(endlessExtent_ / voxelSize_));

			s.endless_ = true;
			s.size_.x = s.size_.z = extent;
			s.origin_.x = centre.x - 0.5f * endlessExtent_;
			s.origin_.z = centre.z - 0.5f * endlessExtent_;
		}

		return s;
	}

	/*!
		@brief
			Composes the recipe from the current choices, keeping the error text when it fails,
			and counts the change as the user's unless it was a quiet catch-up.
		@param library
			The biome library to compose against
		*/
	void TerrainPanel::recompose(const voxel::BiomeLibrary& library)
	{
		stale_ = false;
		composedFrom_ = &library;
		if (!quiet_)
			++userVersion_;
		quiet_ = false;
		composed_.reset();
		problem_.clear();
		try
		{
			composed_ = voxel::composeBiomeRecipe(library, settings(library));
		}
		catch (const std::exception& e)
		{
			problem_ = e.what();
		}
	}

	/*!
		@brief
			Takes size, position, seed and, for biome terrain, every biome setting from a recipe.

			An endless recipe puts the box back to 102 m in the terrain's middle. Only biomes the
			library still has are ticked again.
		@param recipe
			The recipe the terrain was generated from
		@param library
			The biome library, or null to take every biome name as is
		*/
	void TerrainPanel::adopt(const voxel::Recipe& recipe, const voxel::BiomeLibrary* library)
	{
		adopt(recipe.origin_, recipe.size_, recipe.voxelSize_);
		seed_ = recipe.seed_;

		// A streamed terrain is kilometres wide; the box goes back to a size a
		// viewport can show, in the terrain's middle, and keeps its height.
		endless_ = recipe.stream_.endless_;
		if (endless_)
		{
			endlessExtent_ = static_cast<float>(recipe.size_.x) * voxelSize_;

			const glm::vec3 centre = region_.min_ + 0.5f * region_.size_;
			region_.size_.x = region_.size_.z = 102.4f;
			region_.min_.x = centre.x - 51.2f;
			region_.min_.z = centre.z - 51.2f;
		}
		if (!recipe.biomes_.enabled_)
			return;

		blending_ = recipe.biomes_.blending_;
		biomeSize_ = recipe.biomes_.size_;
		// Only biomes the library still has can be ticked again.
		std::vector<std::string> names;
		for (const auto& biome : recipe.biomes_.entries_)
			if (!library || library->find(biome.name_))
				names.push_back(biome.name_);
		if (!names.empty())
			chosen_ = std::move(names);
	}

	/*!
		@brief
			Takes the region from terrain with no recipe, and marks the next recompose as quiet.
		@param origin
			Lowest corner of the terrain, in metres
		@param sizeVoxels
			Size of the terrain in voxels
		@param voxelSize
			Voxel edge length in metres; 0.1 when not positive
		*/
	void TerrainPanel::adopt(const glm::vec3& origin, const glm::ivec3& sizeVoxels, float voxelSize)
	{
		voxelSize_ = voxelSize > 0.f ? voxelSize : 0.1f;
		region_.min_ = origin;
		region_.size_ = glm::vec3(sizeVoxels) * voxelSize_;
		stale_ = true;
		quiet_ = true;
	}

	/*!
		@brief
			Draws the panel body: Level Terrain, the Create / Edit tabs, Rendering and Stats.

			Which tab shows decides editing(). Call between ImGui::Begin and End.
		@param context
			What the panel needs to know about the editor this frame
		@param actions
			Where the panel sends what the user asked for
		*/
	void TerrainPanel::draw(const Context& context, const Actions& actions)
	{
		// ----------------------- Level Terrain ------------------------

		if (ImGui::CollapsingHeader("Level Terrain", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const std::string& map = context.mapFile;
			ImGui::Text(
				"File: %s",
				!map.empty()         ? map.c_str()
				: context.hasTerrain ? "(not saved yet)"
									 : "(none)");
			if (context.terrainModified)
			{
				ImGui::SameLine();
				ImGui::TextColored(kWarningText, "modified");
			}
			ImGui::Text(
				"Status: %s%s",
				context.hasTerrain ? (context.terrainReady ? "ready" : "building") : "no terrain",
				context.busy ? (" - " + context.stage).c_str() : "");
			ImGui::BeginDisabled(!context.levelLoaded);
			if (ImGui::Button("Choose map...") && actions.chooseMap)
				actions.chooseMap();
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!context.terrainModified || map.empty());
			if (ImGui::Button("Revert terrain") && actions.revertTerrain)
				actions.revertTerrain();
			ImGui::EndDisabled();
			tooltip("Throw away generated or edited terrain and reload the saved map.");
			ImGui::SameLine();
			ImGui::BeginDisabled(!context.levelLoaded || (!context.hasTerrain && map.empty()));
			if (ImGui::Button("Remove terrain") && actions.removeTerrain)
				actions.removeTerrain();
			ImGui::EndDisabled();
		}

		// ---------------------------- Tabs ----------------------------

		// Create makes terrain; Edit works on what is there. Which one shows also decides what
		// the viewport's left button does: move the region box, or drive a tool.
		editing_ = false;
		if (ImGui::BeginTabBar("##terrainTabs"))
		{
			if (ImGui::BeginTabItem("Create"))
			{
				drawCreateTab(context, actions);
				ImGui::EndTabItem();
			}
			if (context.drawEditTab && ImGui::BeginTabItem("Edit"))
			{
				editing_ = true;
				context.drawEditTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::Spacing();

		// ------------------------- Rendering --------------------------

		if (context.render && ImGui::CollapsingHeader("Rendering"))
		{
			voxel::RenderSettings& rs = *context.render;
			ImGui::SliderFloat("Subdivision px", &rs.subDivisionSize_, 16.f, 256.f, "%.0f");
			tooltip("A LoD node projecting larger than this (squared) is refined. The default is 64.");
			ImGui::Checkbox("Wireframe", &rs.wireframe_);
			ImGui::SameLine();
			ImGui::Checkbox("LoD colours", &rs.lodColours_);
			ImGui::SameLine();
			ImGui::Checkbox("Section bounds", &rs.sectionBounds_);
			ImGui::Checkbox("Ambient occlusion", &rs.ambientOcclusion_);
			tooltip("Applies to newly meshed sections.");
		}

		// --------------------------- Stats ----------------------------

		if (context.stats && ImGui::CollapsingHeader("Stats"))
		{
			const voxel::Stats& st = *context.stats;
			if (context.hasTerrain)
			{
				const glm::ivec3& size = context.terrainVoxels;
				ImGui::Text(
					"Voxels: %d x %d x %d at %.2f m",
					size.x,
					size.y,
					size.z,
					context.terrainVoxelSize);
			}
			ImGui::Text("Sections: %u dense, %u uniform", st.denseSections_, st.uniformSections_);
			ImGui::Text("Meshes resident: %u nodes, %u quads", st.meshedNodes_, st.residentQuads_);
			ImGui::Text("Last frame: %u draws, %u quads", st.drawsLastFrame_, st.quadsLastFrame_);
			ImGui::Text("Bodies: %u", st.bodies_);
			ImGui::Text(
				"GPU arena: %.1f / %.1f MB",
				st.gpuBytesUsed_ / 1048576.0,
				st.gpuBytesCapacity_ / 1048576.0);
			ImGui::Text(
				"Section pool: %.1f MB in use, %.1f MB reserved, peak %.1f MB",
				st.poolBytesInUse_ / 1048576.0,
				st.poolBytesReserved_ / 1048576.0,
				st.poolBytesPeak_ / 1048576.0);
			tooltip(
				"Every dense 32^3 section in memory - all LoD levels and the undo copies - lives in one pool\n"
				"of 32 KiB blocks, reserved from the system 2 MiB at a time.");
			ImGui::Text(
				"Pool traffic: %llu blocks handed out from %llu system allocations",
				static_cast<unsigned long long>(st.poolAllocations_),
				static_cast<unsigned long long>(st.poolSlabsCreated_));
			ImGui::Text("Timings: load/generate %.0f ms, mip %.0f ms", st.generateMs_, st.mipMs_);

			if (st.endless_)
			{
				ImGui::SeparatorText("Streaming");
				ImGui::Text(
					"Cells by finest level held: %u / %u / %u / %u",
					st.streamCells_[0], st.streamCells_[1], st.streamCells_[2], st.streamCells_[3]);
				tooltip("A cell is 25.6 m a side. Level 0 is full detail, round the camera; every cell holds level 3.");
				ImGui::Text("Building: %u cells", st.streamBuilding_);
				ImGui::Text(
					"Tiles: %.0f MB cached, %llu built, %llu reused",
					st.tileCacheBytes_ / 1048576.0,
					static_cast<unsigned long long>(st.tileBuilds_),
					static_cast<unsigned long long>(st.tileHits_));
				ImGui::Text("Kept edits: %u sections, %.2f MB", st.editStoreSections_, st.editStoreBytes_ / 1048576.0);
				tooltip("Hand-edited sections of cells that have streamed out, compressed, for when the camera comes back.");
				if (st.streamRadiusScale_ < 1.f)
					ImGui::TextColored(
						ImVec4(0.95f, 0.75f, 0.30f, 1.f),
						"Over the memory budget: rings at %.0f %%",
						st.streamRadiusScale_ * 100.f);
			}
		}
	}

	/*!
		@brief
			Draws the Create tab: selection settings, biome settings, the composer's problem or
			warning, the Generate button and progress; recomposes when a choice changed.
		@param context
			What the panel needs to know about the editor this frame
		@param actions
			Where the panel sends what the user asked for
		*/
	void TerrainPanel::drawCreateTab(const Context& context, const Actions& actions)
	{
		const float vs = voxelSize_;
		const glm::vec3 minSize{ Limits::kMinHorizontal * vs,
			                     Limits::kMinVertical * vs,
			                     Limits::kMinHorizontal * vs };
		const glm::vec3 maxSize{ Limits::kMaxHorizontal * vs,
			                     Limits::kMaxVertical * vs,
			                     Limits::kMaxHorizontal * vs };
		const auto toGrid = [&](glm::vec3 v)
		{
			return snap_ ? glm::round(v / vs) * vs : v;
		};

		// ------------------------- Selection --------------------------

		if (ImGui::CollapsingHeader("Selection Settings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::BeginDisabled(context.gizmoDragging);
			glm::vec3 size = region_.size_, position = region_.min_;
			if (ImGui::DragFloat3("Size", &size.x, 0.1f, 0.f, 0.f, "%.1f m"))
			{
				region_.size_ = glm::clamp(toGrid(size), minSize, maxSize);
				stale_ = true;
			}
			tooltip("Width (X), height (Y) and depth (Z) of the terrain.\nThe box in the viewport shows it; "
			        "drag its dots to resize.");
			if (ImGui::DragFloat3("Position", &position.x, 0.1f, 0.f, 0.f, "%.1f m"))
			{
				region_.min_ = toGrid(position);
				stale_ = true;
			}
			tooltip("The box's lowest corner. Biomes are laid out across the whole world, so moving the box\n"
			        "slides it over that layout: the same place always holds the same biome and landform.\n"
			        "Erosion and lakes are worked out per terrain, so fine detail does change. Drag the "
			        "arrows to move.");
			ImGui::EndDisabled();
			const glm::ivec3 voxels(glm::round(region_.size_ / vs));
			ImGui::TextDisabled("%d x %d x %d voxels of %.2f m", voxels.x, voxels.y, voxels.z, vs);
			if (ImGui::Checkbox("Snap to Voxels", &snap_) && snap_)
			{
				region_.min_ = toGrid(region_.min_);
				region_.size_ = toGrid(region_.size_);
				stale_ = true;
			}
			tooltip("Keeps size and position on whole voxels while editing.\nGenerating always rounds to "
			        "whole voxels.");

			if (ImGui::Checkbox("Endless (streamed)", &endless_))
				stale_ = true;
			tooltip("Terrain that is generated round the camera as it moves, instead of all at once: only what is\n"
			        "near is kept in memory, at full detail close by and coarser further off. The box then gives\n"
			        "only the height, and the middle of the terrain.\n\n"
			        "Rivers and lakes never cross a tile (102 m), hand edits last for the session only, and the\n"
			        "level saves as its recipe alone. Tuning: Assets/voxel/stream.json.");
			if (endless_)
			{
				static constexpr float kExtents[] = { 409.6f, 819.2f, 1638.4f, 3276.8f };
				char label[32];
				std::snprintf(label, sizeof(label), "%.0f m", endlessExtent_);

				ImGui::SameLine();
				ImGui::SetNextItemWidth(110.f);
				if (ImGui::BeginCombo("Extent", label))
				{
					for (const float extent : kExtents)
					{
						std::snprintf(label, sizeof(label), "%.0f m", extent);
						if (ImGui::Selectable(label, extent == endlessExtent_))
						{
							endlessExtent_ = extent;
							stale_ = true;
						}
					}
					ImGui::EndCombo();
				}
				tooltip("How far the terrain reaches, side to side. It costs nothing until the player gets there.");
			}

			ImGui::Checkbox("Show region", &showRegion_);
			tooltip("Shows the box and its handles in the viewport.");
			ImGui::SameLine();
			if (ImGui::SmallButton("Frame") && actions.frame)
				actions.frame();
			tooltip("Moves the camera to where the whole region is in view (Home in the viewport).");
		}

		// --------------------------- Biomes ---------------------------

		if (ImGui::CollapsingHeader("Biome Settings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!context.library)
			{
				ImGui::TextColored(kErrorText, "The biome library did not load:");
				ImGui::TextWrapped("%s", context.libraryError.c_str());
			}
			else
			{
				ImGui::TextUnformatted("Biomes");
				ImGui::Indent();
				for (const auto& biome : context.library->biomes())
				{
					const auto at = std::find(chosen_.begin(), chosen_.end(), biome.name_);
					bool ticked = at != chosen_.end();
					ImGui::PushID(biome.name_.c_str());
					ImGui::ColorButton(
						"##swatch",
						ImVec4(biome.colour_.r, biome.colour_.g, biome.colour_.b, 1),
						ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
						ImVec2(14, 14));
					ImGui::SameLine();
					if (ImGui::Checkbox(titled(biome.name_).c_str(), &ticked))
					{
						if (ticked)
							chosen_.push_back(biome.name_);
						else
							chosen_.erase(at);
						stale_ = true;
					}
					ImGui::PopID();
				}
				ImGui::Unindent();
				if (ImGui::SliderFloat("Blending", &blending_, 0.f, 1.f, "%.3f"))
					stale_ = true;
				tooltip("How wide the transition between two biomes is.\n0 is a crisp material edge; the "
				        "ground itself always meets smoothly.");
				if (ImGui::DragFloat(
						"Biome Size",
						&biomeSize_,
						1.f,
						Limits::kMinBiomeSize,
						Limits::kMaxBiomeSize,
						"%.0f m",
						ImGuiSliderFlags_AlwaysClamp))
					stale_ = true;
				tooltip("How far across one patch of a biome is. Landforms scale with it:\nmountains only "
				        "reach their full height in large patches.");
				if (ImGui::InputScalar("Seed", ImGuiDataType_U64, &seed_))
					stale_ = true;
				ImGui::SameLine();
				if (ImGui::SmallButton("Random"))
				{
					seed_ = std::random_device{}()
					        ^ (static_cast<unsigned long long>(std::random_device{}()) << 17);
					stale_ = true;
				}
			}
		}

		// ------------------------- Recompose --------------------------

		if (context.library && (stale_ || composedFrom_ != context.library) && !context.gizmoDragging)
			recompose(*context.library);

		ImGui::Spacing();
		if (!problem_.empty())
			ImGui::TextColored(kErrorText, "%s", problem_.c_str());
		else if (composed_ && composed_->biomes_.reliefScale_ < 0.995f)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, kWarningText);
			ImGui::TextWrapped(
				"The box is too low for these biomes: landforms are squeezed to %.0f%% of their height. "
			    "Raise Size Y for full relief.",
				composed_->biomes_.reliefScale_ * 100.f);
			ImGui::PopStyleColor();
		}

		// -------------------------- Generate --------------------------

		const bool canGenerate = composed_ && context.levelLoaded && !context.busy && !context.gizmoDragging;
		ImGui::BeginDisabled(!canGenerate);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.95f, 1));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.52f, 1.00f, 1));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.36f, 0.85f, 1));
		if (ImGui::Button("Generate", ImVec2(-1, 30)) && actions.generate)
			actions.generate(*composed_);
		ImGui::PopStyleColor(3);
		ImGui::EndDisabled();
		if (!context.levelLoaded)
			tooltip("Open or create a level first: terrain belongs to a level.");

		// -------------------------- Progress --------------------------

		if (context.busy)
		{
			ImGui::ProgressBar(context.progress, ImVec2(-70, 0), context.stage.c_str());
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(-1, 0)) && actions.cancel)
				actions.cancel();
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("Generating replaces the level's terrain and cannot be undone. The terrain is "
		                   "saved together with the level (Ctrl+S).");
		ImGui::PopStyleColor();
	}
}
