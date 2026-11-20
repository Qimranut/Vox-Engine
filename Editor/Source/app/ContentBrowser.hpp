/*************************************************************************************************************
\file       ContentBrowser.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the Editor's Content Browser panel (a folder tree of the asset root beside the current
            folder as typed tiles) and the asset-name validity check used when creating files and folders.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <filesystem>
#include <functional>
#include <string>
#include <vector>


namespace vox::editor
{
	/*!
		@brief
			Checks a name the user typed for a new file or folder.
		@param name
			A file or folder name typed by the user, without extension
		@return
			true when it is usable as one path component on Windows: not empty, no separators or
			reserved characters, no leading or trailing space or dot
		*/
	bool isValidAssetName(const std::string& name);

	/**
	 * The Content Browser panel: a folder tree of the asset root on the left, the
	 * current folder as tiles on the right. It only looks at the disk; everything
	 * it wants done to the level is reported through Actions.
	 *
	 */
	class ContentBrowser
	{
	public:

		/**
		 * What the browser asks its owner to do. Paths are asset-relative.
		 *
		 */
		struct Actions
		{
			std::function<void(const std::string& relative)>			openLevel;	// .voxscene, double-click
			std::function<void(const std::string& relative)>			useTerrain;	// .voxmap, double-click
			std::function<void(const std::string& relative)>			pickRecipe;	// .voxgen, double-click
			std::function<void(const std::string& relative)>			preview;	// any other file, single click
			std::function<void(const std::string& folder)>				newLevel;	// "+" > New level
			std::function<void(const std::string& message, bool error)>	report;
		};

		/*!
			@brief
				Re-reads the asset tree. Dot folders (.cooked) are generated output and stay hidden.
			@param root
				The asset root; every path the browser holds is relative to it
			*/
		void rescan(const std::filesystem::path& root);

		/*!
			@brief
				Draws the panel body; call between ImGui::Begin and End.
			@param actions
				What to call when the user opens, previews or creates something
			@param openLevel
				Asset-relative path of the level being edited, outlined in the grid; may be empty
			@param reserveBottom
				Height to leave under the browser for the caller's own widgets
			*/
		void draw(const Actions& actions, const std::string& openLevel, float reserveBottom);

		/*!
			@brief
				Clears the selected file or folder.
			*/
		void clearSelection() { selected_.clear(); }

		/*!
			@brief
				Returns every file under the asset root.
			@return
				Asset-relative generic paths, sorted
			*/
		const std::vector<std::string>&	files() const		{ return files_; }
		/*!
			@brief
				Returns every folder under the asset root, dot folders excluded.
			@return
				Asset-relative generic paths, sorted
			*/
		const std::vector<std::string>&	folders() const		{ return folders_; }
		/*!
			@brief
				Returns the selected file or folder.
			@return
				Its asset-relative path; empty when nothing is selected
			*/
		const std::string&				selected() const	{ return selected_; }

	private:

		/*!
			@brief
				Draws the row above the panes: create menu, Up, Refresh, the search box and the breadcrumb.
			@param actions
				What to call when the user creates a level or refreshes
			*/
		void toolbar(const Actions& actions);

		/*!
			@brief
				Draws the folder tree's nodes whose parent is the given folder, recursively.
			@param parent
				Asset-relative folder whose children are drawn; empty for the root
			*/
		void tree(const std::string& parent);

		/*!
			@brief
				Draws the current folder as a row of buttons, one per path component.
			*/
		void breadcrumb();

		/*!
			@brief
				Draws the tiles of the current folder, or of every file that matches the search.
			@param actions
				What to call when a tile is opened or previewed
			@param openLevel
				Asset-relative path of the level being edited, outlined in the grid
			*/
		void grid(const Actions& actions, const std::string& openLevel);

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
		bool tile(const std::string& relative, bool folder, bool outlined);

		/*!
			@brief
				Enters a folder, or hands a file to the action its extension calls for.
			@param actions
				What to call for a file
			@param relative
				Asset-relative path of the file or folder
			@param folder
				Whether it is a folder
			*/
		void activate(const Actions& actions, const std::string& relative, bool folder);

		/*!
			@brief
				Draws the right-click menu of the tile drawn last.
			@param actions
				What to call for the menu's items
			@param relative
				Asset-relative path of the tile's file or folder
			@param folder
				Whether it is a folder
			*/
		void contextMenu(const Actions& actions, const std::string& relative, bool folder);
		/*!
			@brief
				Opens Windows Explorer on a folder, or on a file's folder with the file selected.
			@param relative
				Asset-relative path of the file or folder
			*/
		void showInExplorer(const std::string& relative) const;

		/*!
			@brief
				Draws the modal that asks for a new folder's name and creates it in the current folder.
			@param actions
				Where the result is reported
			*/
		void newFolderPopup(const Actions& actions);

		std::filesystem::path		root_;
		std::vector<std::string>	files_;
		std::vector<std::string>	folders_;

		// Asset-relative; an empty current_ is the asset root.
		std::string	current_;
		std::string	selected_;

		char		filter_[128]		{};
		char		folderName_[128]	{};
		bool		askFolderName_		{};
	};
}
