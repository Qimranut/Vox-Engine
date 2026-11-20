/*************************************************************************************************************
\file       EditorUIDialogs.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      EditorUI modal popups: the unsaved-changes prompt, Save level as (with the terrain-copy choice
            and overwrite warnings), Open level (filterable, with recent levels) and Choose terrain map.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "EditorUI.hpp"

// Editor
#include "EditorUICommon.hpp"

// Voxel
#include "voxel/sys/voxel_sys.hpp"

// ImGui
#include <imgui.h>

// stds
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>


namespace vox::editor
{
	/*!
		@brief
			Draws every modal popup the editor can ask for; called at the root ID level each frame.
		*/
	void EditorUI::dialogs()
	{
		unsavedPopup();
		saveAsPopup();
		openLevelPopup();
		chooseMapPopup();
	}

	/*!
		@brief
			Draws the unsaved-changes prompt for the pending command: Save (then resume it), Don't save
			(run it now) or Cancel.
		*/
	void EditorUI::unsavedPopup()
	{
		if (confirm_)
		{
			ImGui::OpenPopup("Unsaved changes");
			confirm_ = false;
		}

		if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::Text("\"%s\" has changes that are not saved:", levelName().c_str());
		if (scene_->dirty())
			ImGui::BulletText("Scene objects and settings");
		if (voxel_->modified())
			ImGui::BulletText("Terrain (generated or edited since the map was written)");

		ImGui::Spacing();

		if (ImGui::Button("Save"))
		{
			// The command waits for the save, which may still need a name from the Save As popup.
			afterSave_ = pending_;
			pending_ = Command::None;
			ImGui::CloseCurrentPopup();
			attempt([&] { saveLevel(); });
		}

		ImGui::SameLine();
		if (ImGui::Button("Don't save"))
		{
			ImGui::CloseCurrentPopup();
			attempt([&] { perform(pending_); });
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			pending_ = Command::None;
			newLevelThenSaveAs_ = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	/*!
		@brief
			Draws the Save level as popup: name, folder, whether the level gets its own copy of the
			terrain, what will be written and overwritten, and the Save / Cancel buttons.
		*/
	void EditorUI::saveAsPopup()
	{
		if (askSaveAs_)
		{
			ImGui::OpenPopup("Save level as");
			askSaveAs_ = false;
		}

		if (!ImGui::BeginPopupModal("Save level as", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		// ---------------------- Name and folder -----------------------

		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();

		const bool enter =
			ImGui::InputText("Name", saveName_, sizeof(saveName_), ImGuiInputTextFlags_EnterReturnsTrue);

		if (ImGui::BeginCombo("Folder", ("Assets/" + saveFolder_).c_str()))
		{
			if (ImGui::Selectable("Assets/", saveFolder_.empty()))
				saveFolder_.clear();

			for (const auto& folder : browser_.folders())
				if (ImGui::Selectable(("Assets/" + folder).c_str(), folder == saveFolder_))
					saveFolder_ = folder;

			ImGui::EndCombo();
		}

		// ------------------------ Save targets ------------------------

		const std::string name = saveName_;
		const bool valid = isValidAssetName(name);
		const std::string sceneRelative = (saveFolder_.empty() ? "" : saveFolder_ + "/") + name + ".voxscene";
		const std::filesystem::path sceneFile = resources_->root() / sceneRelative;
		const std::string ownTerrain = "maps/" + name + ".voxmap";

		// Writing a copy is only a choice when the level has terrain that lives somewhere else.
		const bool canCopyTerrain = voxel_->hasWorld() && scene_->world() != ownTerrain;

		std::string terrain;
		if (canCopyTerrain && saveOwnTerrain_)
			terrain = ownTerrain;
		else if (voxel_->modified())
			terrain = scene_->world().empty() ? ownTerrain : scene_->world();

		ImGui::Spacing();

		if (canCopyTerrain)
		{
			ImGui::Checkbox("Give this level its own copy of the terrain", &saveOwnTerrain_);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"On: the terrain is written to maps/<name>.voxmap, so other levels sharing the\ncurrent "
					"map are not affected. Off: the level keeps pointing at the current map.");
		}

		// -------------------- Summary and warnings --------------------

		std::error_code ec;
		bool overwrites = false;

		if (valid)
		{
			ImGui::TextDisabled("Scene:   Assets/%s", sceneRelative.c_str());

			const std::string& shownTerrain = terrain.empty() ? scene_->world() : terrain;
			ImGui::TextDisabled(
				"Terrain: %s%s%s",
				shownTerrain.empty() ? "" : "Assets/",
				shownTerrain.empty() ? "none" : shownTerrain.c_str(),
				terrain.empty() && !shownTerrain.empty() ? " (unchanged, not rewritten)" : "");

			const bool sceneTaken =
				std::filesystem::exists(sceneFile, ec)
				&& !(!scene_->path().empty() && std::filesystem::equivalent(sceneFile, scene_->path(), ec));
			const bool terrainTaken =
				!terrain.empty() && terrain != scene_->world()
				&& std::filesystem::exists(resources_->root() / terrain, ec);

			if (sceneTaken)
				ImGui::TextColored(
					kErrorText,
					"A level with this name already exists here and will be overwritten.");

			if (terrainTaken)
				ImGui::TextColored(
					kErrorText,
					"Assets/%s already exists and will be overwritten.",
					terrain.c_str());

			if (!terrain.empty()
				&& std::filesystem::exists((resources_->root() / terrain).replace_extension(".voxgen"), ec))
				ImGui::TextColored(
					kWarningText,
					"A recipe shares this map's name: editing the recipe later regenerates the map.");

			overwrites = sceneTaken || terrainTaken;
		}
		else if (!name.empty())
			ImGui::TextColored(kErrorText, "Use letters, digits, spaces, - and _ only.");

		// -------------------------- Buttons ---------------------------

		ImGui::Spacing();

		ImGui::BeginDisabled(!valid);
		if (ImGui::Button(overwrites ? "Overwrite" : "Save") || (enter && valid && !overwrites))
		{
			ImGui::CloseCurrentPopup();
			attempt([&] { commitSave(sceneFile, terrain); });
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			afterSave_ = Command::None;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	/*!
		@brief
			Draws the Open level popup: a filter box, recent levels and every .voxscene, opening the
			picked one through request().
		*/
	void EditorUI::openLevelPopup()
	{
		if (askOpenLevel_)
		{
			ImGui::OpenPopup("Open level");
			askOpenLevel_ = false;
			openFilter_[0] = 0;
			openPick_.clear();
			refresh();
		}

		ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_Appearing);
		if (!ImGui::BeginPopupModal("Open level"))
			return;

		// --------------------------- Filter ---------------------------

		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();

		ImGui::SetNextItemWidth(-1);
		const bool enter = ImGui::InputTextWithHint(
			"##Filter",
			"Type to filter levels, Enter opens the first match",
			openFilter_,
			sizeof(openFilter_),
			ImGuiInputTextFlags_EnterReturnsTrue);

		// --------------------------- Levels ---------------------------

		bool open = false;
		const auto row = [&](const std::string& relative)
		{
			if (ImGui::Selectable(
					relative.c_str(),
					openPick_ == relative,
					ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_NoAutoClosePopups))
			{
				openPick_ = relative;
				open = open || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			}
		};

		const auto& files = browser_.files();
		ImGui::BeginChild("Levels", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);

		if (!openFilter_[0] && !recent_.items().empty())
		{
			ImGui::SeparatorText("Recent");
			ImGui::PushID("Recent");
			for (const auto& relative : recent_.items())
				if (std::binary_search(files.begin(), files.end(), relative))
					row(relative);
			ImGui::PopID();

			ImGui::SeparatorText("All levels");
		}

		bool any = false;
		for (const auto& relative : files)
			if (relative.ends_with(".voxscene") && containsNoCase(relative, openFilter_))
			{
				// Enter in the filter box takes the highlighted level, or the first match.
				if (enter && !any && (openPick_.empty() || !containsNoCase(openPick_, openFilter_)))
					openPick_ = relative;

				row(relative);
				any = true;
			}

		open = open || (enter && any);

		if (!any)
			ImGui::TextDisabled(
				openFilter_[0]	? "No level matches the filter."
								: "There are no levels yet. Use File > New Level.");

		ImGui::EndChild();

		// -------------------------- Buttons ---------------------------

		ImGui::BeginDisabled(openPick_.empty());
		if (ImGui::Button("Open"))
			open = true;
		ImGui::EndDisabled();

		ImGui::SameLine();
		const bool cancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);

		if (open && !openPick_.empty())
		{
			ImGui::CloseCurrentPopup();
			request(Command::OpenLevel, resources_->root() / openPick_);
		}
		else if (cancel)
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}

	/*!
		@brief
			Draws the Choose terrain map popup: every .voxmap, making the picked one the level's terrain
			through Command::UseTerrain.
		*/
	void EditorUI::chooseMapPopup()
	{
		if (askChooseMap_)
		{
			ImGui::OpenPopup("Choose terrain map");
			askChooseMap_ = false;
			mapPick_ = scene_->world();
			refresh();
		}

		ImGui::SetNextWindowSize(ImVec2(460, 320), ImGuiCond_Appearing);
		if (!ImGui::BeginPopupModal("Choose terrain map"))
			return;

		ImGui::TextDisabled("The map becomes this level's terrain. Save the level to keep the choice.");

		bool choose = false;
		bool any = false;

		ImGui::BeginChild("Maps", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);

		for (const auto& relative : browser_.files())
		{
			if (!relative.ends_with(".voxmap"))
				continue;

			any = true;

			if (ImGui::Selectable(
					relative.c_str(),
					mapPick_ == relative,
					ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_NoAutoClosePopups))
			{
				mapPick_ = relative;
				choose = choose || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			}
		}

		if (!any)
			ImGui::TextDisabled("No .voxmap yet. Generate terrain and save the level to create one.");

		ImGui::EndChild();

		ImGui::BeginDisabled(mapPick_.empty());
		if (ImGui::Button("Use this map"))
			choose = true;
		ImGui::EndDisabled();

		ImGui::SameLine();
		const bool cancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);

		if (choose && !mapPick_.empty())
		{
			ImGui::CloseCurrentPopup();
			request(Command::UseTerrain, mapPick_);
		}
		else if (cancel)
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}
}
