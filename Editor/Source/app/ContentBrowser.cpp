/*************************************************************************************************************
\file       ContentBrowser.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements the Content Browser panel: scanning the asset root, the folder tree, breadcrumb,
            typed tiles with search, context menus, Show in Explorer and the New folder popup.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "ContentBrowser.hpp"

// ImGui
#include <imgui.h>

// Windows
// WIN32_LEAN_AND_MEAN and NOMINMAX are defined project-wide.
#include <windows.h>
#include <shellapi.h>

// stds
#include <algorithm>
#include <cctype>
#include <cstring>


namespace vox::editor
{
	namespace
	{
		// Grid tile footprint, in pixels.
		constexpr float kTileWidth = 92.0f;
		constexpr float kIconHeight = 52.0f;

		struct Kind
		{
			const char* tag;
			ImU32 colour;
		};

		/*!
			@brief
				Lower-cases a string, ASCII only.
			@param text
				Text to convert
			@return
				The lower-cased copy
			*/
		std::string lower(std::string text)
		{
			std::transform(
				text.begin(),
				text.end(),
				text.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		/*!
			@brief
				Returns the folder part of a generic path.
			@param relative
				Asset-relative path
			@return
				Everything before the last '/'; empty when there is none
			*/
		std::string parentOf(const std::string& relative)
		{
			const auto slash = relative.find_last_of('/');
			return slash == std::string::npos ? std::string{} : relative.substr(0, slash);
		}

		/*!
			@brief
				Returns the last component of a generic path.
			@param relative
				Asset-relative path
			@return
				Everything after the last '/'; the whole path when there is none
			*/
		std::string nameOf(const std::string& relative)
		{
			const auto slash = relative.find_last_of('/');
			return slash == std::string::npos ? relative : relative.substr(slash + 1);
		}

		/*!
			@brief
				Returns a path's extension, lower-cased and with its dot.
			@param relative
				Asset-relative path
			@return
				The extension, e.g. ".voxscene"; empty when the name has no dot
			*/
		std::string extensionOf(const std::string& relative)
		{
			const std::string name = nameOf(relative);
			const auto dot = name.find_last_of('.');
			return dot == std::string::npos ? std::string{} : lower(name.substr(dot));
		}

		/*!
			@brief
				Tests whether an extension equals any in a list.
			@param ext
				Extension to test
			@param list
				Extensions to compare against
			@return
				True when ext matches one of them
			*/
		bool isOneOf(const std::string& ext, std::initializer_list<const char*> list)
		{
			return std::any_of(list.begin(), list.end(), [&](const char* e) { return ext == e; });
		}

		/*!
			@brief
				Returns what the tile says the file is, chosen by extension.

				The tag is the whole icon, so keep it short.
			@param relative
				Asset-relative path of the file
			@return
				The tile's tag and colour
			*/
		Kind kindOf(const std::string& relative)
		{
			const std::string ext = extensionOf(relative);
			if (ext == ".voxscene")
				return { "LEVEL", IM_COL32(58, 122, 214, 255) };
			if (ext == ".voxmap")
				return { "MAP", IM_COL32(64, 158, 92, 255) };
			if (ext == ".voxgen")
				return { "RECIPE", IM_COL32(38, 150, 150, 255) };
			if (isOneOf(ext, { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".dds" }))
				return { "TEX", IM_COL32(146, 92, 196, 255) };
			if (isOneOf(ext, { ".obj", ".fbx", ".gltf", ".glb", ".dae" }))
				return { "MODEL", IM_COL32(212, 132, 48, 255) };
			if (isOneOf(ext, { ".wav", ".mp3", ".ogg" }))
				return { "AUDIO", IM_COL32(204, 84, 128, 255) };
			if (ext == ".json")
				return { "JSON", IM_COL32(110, 116, 128, 255) };
			if (ext == ".texmeta")
				return { "META", IM_COL32(110, 116, 128, 255) };
			return { "FILE", IM_COL32(110, 116, 128, 255) };
		}
	}

	/*!
		@brief
			Checks a name the user typed for a new file or folder.
		@param name
			A file or folder name typed by the user, without extension
		@return
			true when it is 1-100 characters, has no control or reserved characters and no leading or
			trailing space or dot
		*/
	bool isValidAssetName(const std::string& name)
	{
		if (name.empty() || name.size() > 100)
			return false;

		if (name.front() == ' ' || name.back() == ' ' || name.front() == '.' || name.back() == '.')
			return false;

		return std::none_of(
			name.begin(),
			name.end(),
			[](unsigned char c) { return c < 32 || std::strchr("<>:\"/\\|?*", c); });
	}

	/*!
		@brief
			Re-reads the asset tree, skipping dot folders, and drops a current folder or selection that
			no longer exists.
		@param root
			The asset root; every path the browser holds is relative to it
		*/
	void ContentBrowser::rescan(const std::filesystem::path& root)
	{
		root_ = root;
		files_.clear();
		folders_.clear();

		std::error_code ec;
		if (!std::filesystem::exists(root_, ec))
			return;

		for (auto it = std::filesystem::recursive_directory_iterator(root_, ec);
		     !ec && it != std::filesystem::recursive_directory_iterator();
		     it.increment(ec))
		{
			const std::string relative = it->path().lexically_relative(root_).generic_string();
			if (it->is_directory(ec))
			{
				// Dot folders hold generated output (.cooked textures), not assets to pick.
				if (it->path().filename().string().starts_with('.'))
				{
					it.disable_recursion_pending();
					continue;
				}

				folders_.push_back(relative);
			}
			else if (it->is_regular_file(ec))
				files_.push_back(relative);
		}

		std::sort(files_.begin(), files_.end());
		std::sort(folders_.begin(), folders_.end());

		if (!current_.empty() && std::find(folders_.begin(), folders_.end(), current_) == folders_.end())
			current_.clear();

		if (!selected_.empty() && std::find(files_.begin(), files_.end(), selected_) == files_.end()
		    && std::find(folders_.begin(), folders_.end(), selected_) == folders_.end())
			selected_.clear();
	}

	/*!
		@brief
			Draws the folder tree's nodes whose parent is the given folder, recursively; clicking one
			makes it the current folder.
		@param parent
			Asset-relative folder whose children are drawn; empty for the root
		*/
	void ContentBrowser::tree(const std::string& parent)
	{
		for (const auto& folder : folders_)
		{
			if (parentOf(folder) != parent)
				continue;

			const bool hasChildren = std::any_of(
				folders_.begin(),
				folders_.end(),
				[&](const std::string& f) { return parentOf(f) == folder; });
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (!hasChildren)
				flags |= ImGuiTreeNodeFlags_Leaf;
			if (folder == current_)
				flags |= ImGuiTreeNodeFlags_Selected;

			const bool open = ImGui::TreeNodeEx(folder.c_str(), flags, "%s", nameOf(folder).c_str());
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
				current_ = folder;

			if (open)
			{
				tree(folder);
				ImGui::TreePop();
			}
		}
	}

	/*!
		@brief
			Draws the current folder as a row of buttons, one per path component; a click goes there.
		*/
	void ContentBrowser::breadcrumb()
	{
		if (ImGui::SmallButton("Assets"))
			current_.clear();

		size_t start = 0;
		while (start < current_.size())
		{
			size_t end = current_.find('/', start);
			if (end == std::string::npos)
				end = current_.size();

			ImGui::SameLine(0, 4);
			ImGui::TextDisabled(">");
			ImGui::SameLine(0, 4);
			ImGui::PushID(static_cast<int>(end));
			if (ImGui::SmallButton(current_.substr(start, end - start).c_str()))
			{
				current_ = current_.substr(0, end);
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
			start = end + 1;
		}
	}

	/*!
		@brief
			Draws one tile: icon, name and, for the open level, an outline.
		@param relative
			Asset-relative path of the file or folder
		@param folder
			Whether it is a folder
		@param outlined
			Whether to draw the outline that marks the open level
		@return
			Whether the tile was clicked
		*/
	bool ContentBrowser::tile(const std::string& relative, bool folder, bool outlined)
	{
		const float height = kIconHeight + ImGui::GetTextLineHeight() * 2 + 10.0f;
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 corner(origin.x + kTileWidth, origin.y + height);
		const bool clicked = ImGui::Selectable(
			"##tile",
			relative == selected_,
			ImGuiSelectableFlags_AllowDoubleClick,
			ImVec2(kTileWidth, height));

		auto* draw = ImGui::GetWindowDrawList();
		const float centre = origin.x + kTileWidth * 0.5f, top = origin.y + 5.0f;
		if (folder)
		{
			draw->AddRectFilled(
				ImVec2(centre - 28, top + 2),
				ImVec2(centre - 4, top + 14),
				IM_COL32(196, 148, 52, 255),
				3.0f);
			draw->AddRectFilled(
				ImVec2(centre - 28, top + 9),
				ImVec2(centre + 28, top + kIconHeight - 6),
				IM_COL32(232, 184, 76, 255),
				4.0f);
		}
		else
		{
			const Kind kind = kindOf(relative);
			draw->AddRectFilled(
				ImVec2(centre - 26, top),
				ImVec2(centre + 26, top + kIconHeight - 4),
				kind.colour,
				5.0f);
			const ImVec2 tag = ImGui::CalcTextSize(kind.tag);
			draw->AddText(
				ImVec2(centre - tag.x * 0.5f, top + (kIconHeight - 4 - tag.y) * 0.5f),
				IM_COL32_WHITE,
				kind.tag);
		}

		if (outlined)
			draw->AddRect(origin, corner, ImGui::GetColorU32(ImGuiCol_CheckMark), 4.0f, 0, 2.0f);

		// Two wrapped lines of name; anything longer is clipped and shown in the tooltip.
		const std::string name = nameOf(relative);
		const float wrap = kTileWidth - 8.0f;
		const float width = std::min(wrap, ImGui::CalcTextSize(name.c_str()).x);
		draw->PushClipRect(origin, corner, true);
		draw->AddText(
			nullptr,
			0.0f,
			ImVec2(centre - width * 0.5f, origin.y + kIconHeight + 4.0f),
			ImGui::GetColorU32(ImGuiCol_Text),
			name.c_str(),
			nullptr,
			wrap);
		draw->PopClipRect();

		return clicked;
	}

	/*!
		@brief
			Enters a folder (clearing the search), or hands a file to the action its extension calls for.
		@param actions
			What to call for a file
		@param relative
			Asset-relative path of the file or folder
		@param folder
			Whether it is a folder
		*/
	void ContentBrowser::activate(const Actions& actions, const std::string& relative, bool folder)
	{
		if (folder)
		{
			current_ = relative;
			filter_[0] = 0;
			return;
		}

		const std::string ext = extensionOf(relative);
		if (ext == ".voxscene")
			actions.openLevel(relative);
		else if (ext == ".voxmap")
			actions.useTerrain(relative);
		else if (ext == ".voxgen")
			actions.pickRecipe(relative);
		else
			actions.preview(relative);
	}

	/*!
		@brief
			Draws the right-click menu of the tile drawn last: open, new level, copy path, Show in Explorer.
		@param actions
			What to call for the menu's items
		@param relative
			Asset-relative path of the tile's file or folder
		@param folder
			Whether it is a folder
		*/
	void ContentBrowser::contextMenu(const Actions& actions, const std::string& relative, bool folder)
	{
		if (!ImGui::BeginPopupContextItem("TileMenu"))
			return;

		selected_ = relative;
		const std::string ext = extensionOf(relative);
		const char* primary = folder               ? "Open folder"
		                      : ext == ".voxscene" ? "Open level"
		                      : ext == ".voxmap"   ? "Use as this level's terrain"
		                      : ext == ".voxgen"   ? "Generate this level's terrain from it"
		                                           : "Preview";
		if (ImGui::MenuItem(primary))
			activate(actions, relative, folder);
		if (folder && ImGui::MenuItem("New level here..."))
			actions.newLevel(relative);

		ImGui::Separator();
		if (ImGui::MenuItem("Copy path"))
			ImGui::SetClipboardText(relative.c_str());
		if (ImGui::MenuItem("Show in Explorer"))
			showInExplorer(relative);
		ImGui::EndPopup();
	}

	/*!
		@brief
			Opens Windows Explorer on a folder, or on a file's folder with the file selected.
		@param relative
			Asset-relative path of the file or folder
		*/
	void ContentBrowser::showInExplorer(const std::string& relative) const
	{
		std::error_code ec;
		auto full = std::filesystem::absolute(root_ / relative, ec).lexically_normal();
		full.make_preferred();
		const std::wstring arguments = std::filesystem::is_directory(full, ec)
		                                   ? L"\"" + full.native() + L"\""
		                                   : L"/select,\"" + full.native() + L"\"";
		ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
	}

	/*!
		@brief
			Draws the tiles of the current folder, or of every file that matches the search, and handles
			their clicks: levels, maps, recipes and folders open on double-click, other files preview on
			a single click.
		@param actions
			What to call when a tile is opened or previewed
		@param openLevel
			Asset-relative path of the level being edited, outlined in the grid
		*/
	void ContentBrowser::grid(const Actions& actions, const std::string& openLevel)
	{
		// Snapshot first: activating a folder changes current_ while the tiles are drawn.
		std::vector<std::pair<std::string, bool>> entries;
		const std::string needle = lower(filter_);
		if (needle.empty())
		{
			for (const auto& f : folders_)
				if (parentOf(f) == current_)
					entries.emplace_back(f, true);
			for (const auto& f : files_)
				if (parentOf(f) == current_)
					entries.emplace_back(f, false);
		}
		else
			for (const auto& f : files_)
				if (lower(f).find(needle) != std::string::npos)
					entries.emplace_back(f, false);

		if (entries.empty())
		{
			ImGui::TextDisabled(needle.empty() ? "This folder is empty." : "Nothing matches the search.");
			return;
		}

		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const int columns = std::max(
			1,
			static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (kTileWidth + spacing)));
		int column = 0;

		for (const auto& [relative, folder] : entries)
		{
			ImGui::PushID(relative.c_str());
			const bool clicked = tile(relative, folder, !folder && relative == openLevel);
        if (!folder && ImGui::BeginDragDropSource()) {
            const auto ext = extensionOf(relative);
            const char* payload = isOneOf(ext, {".wav", ".mp3"}) ? "VOX_AUDIO_ASSET" :
                isOneOf(ext, {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".dds"}) ? "VOX_TEXTURE_ASSET" :
                isOneOf(ext, {".obj", ".fbx", ".gltf", ".glb", ".dae"}) ? "VOX_MODEL_ASSET" :
                isOneOf(ext, {".ttf", ".otf"}) ? "VOX_FONT_ASSET" : nullptr;
            if (payload) ImGui::SetDragDropPayload(payload, relative.c_str(), relative.size() + 1);
            ImGui::TextUnformatted(relative.c_str());
            ImGui::EndDragDropSource();
        }

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("%s", relative.c_str());
			contextMenu(actions, relative, folder);
			ImGui::PopID();

			if (clicked)
			{
				selected_ = relative;
				const bool opensOnDoubleClick =
					folder || isOneOf(extensionOf(relative), { ".voxscene", ".voxmap", ".voxgen" });
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (opensOnDoubleClick)
					{
						activate(actions, relative, folder);
						break;
					}
				}
				else if (!opensOnDoubleClick)
					actions.preview(relative);
			}
			if (++column < columns)
				ImGui::SameLine();
			else
				column = 0;
		}
	}

	/*!
		@brief
			Draws the row above the panes: create menu, Up, Refresh, the search box and the breadcrumb.
		@param actions
			What to call when the user creates a level or refreshes
		*/
	void ContentBrowser::toolbar(const Actions& actions)
	{
		// Create menu
		if (ImGui::Button("+"))
			ImGui::OpenPopup("CreateMenu");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Create a level or a folder in the current folder");

		if (ImGui::BeginPopup("CreateMenu"))
		{
			if (ImGui::MenuItem("New level..."))
				actions.newLevel(current_);

			if (ImGui::MenuItem("New folder..."))
			{
				askFolderName_ = true;
				folderName_[0] = 0;
			}

			ImGui::EndPopup();
		}

		// Up
		ImGui::SameLine();
		ImGui::BeginDisabled(current_.empty());
		if (ImGui::Button("Up"))
			current_ = parentOf(current_);
		ImGui::EndDisabled();

		// Refresh
		ImGui::SameLine();
		if (ImGui::Button("Refresh"))
		{
			rescan(root_);
			actions.report("Asset list refreshed.", false);
		}

		// Search
		ImGui::SameLine();
		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##Search", "Search all assets", filter_, sizeof(filter_));

		ImGui::SameLine();
		breadcrumb();
	}

	/*!
		@brief
			Draws the modal that asks for a new folder's name and creates it in the current folder,
			then rescans and enters it.
		@param actions
			Where the result is reported
		*/
	void ContentBrowser::newFolderPopup(const Actions& actions)
	{
		if (askFolderName_)
		{
			ImGui::OpenPopup("New folder");
			askFolderName_ = false;
		}

		if (ImGui::BeginPopupModal("New folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Create in: Assets/%s", current_.c_str());
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			const bool enter = ImGui::InputText(
				"Name",
				folderName_,
				sizeof(folderName_),
				ImGuiInputTextFlags_EnterReturnsTrue);
			std::error_code ec;
			const bool valid = isValidAssetName(folderName_);
			const bool exists = valid && std::filesystem::exists(root_ / current_ / folderName_, ec);
			if (folderName_[0] && !valid)
				ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "Use letters, digits, spaces, - and _ only.");
			if (exists)
				ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "That name is already taken here.");

			ImGui::BeginDisabled(!valid || exists);
			if (ImGui::Button("Create") || (enter && valid && !exists))
			{
				if (std::filesystem::create_directory(root_ / current_ / folderName_, ec))
				{
					const std::string made =
						current_.empty() ? std::string(folderName_) : current_ + "/" + folderName_;
					rescan(root_);
					current_ = made;
					actions.report("Created folder " + made + ".", false);
				}
				else
					actions.report("Could not create the folder: " + ec.message(), true);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	/*!
		@brief
			Draws the panel body: toolbar, folder tree on the left, tiles on the right, and the New folder
			popup. Call between ImGui::Begin and End.
		@param actions
			What to call when the user opens, previews or creates something
		@param openLevel
			Asset-relative path of the level being edited, outlined in the grid; may be empty
		@param reserveBottom
			Height to leave under the browser for the caller's own widgets
		*/
	void ContentBrowser::draw(const Actions& actions, const std::string& openLevel, float reserveBottom)
	{
		toolbar(actions);

		const float height = std::max(80.0f, ImGui::GetContentRegionAvail().y - reserveBottom);

		// Folder tree, on the left
		ImGui::BeginChild(
			"Folders",
			ImVec2(200.0f, height),
			ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

		ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
			| ImGuiTreeNodeFlags_SpanAvailWidth;
		if (current_.empty())
			rootFlags |= ImGuiTreeNodeFlags_Selected;

		const bool open = ImGui::TreeNodeEx("##AssetsRoot", rootFlags, "Assets");
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			current_.clear();

		if (open)
		{
			tree({});
			ImGui::TreePop();
		}

		ImGui::EndChild();

		// Tiles of the current folder, on the right
		ImGui::SameLine();
		ImGui::BeginChild("Tiles", ImVec2(0, height), ImGuiChildFlags_Borders);
		grid(actions, openLevel);
		ImGui::EndChild();

		newFolderPopup(actions);
	}
}
