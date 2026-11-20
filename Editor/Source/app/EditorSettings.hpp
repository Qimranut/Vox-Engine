/*************************************************************************************************************
\file       EditorSettings.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the Editor's persisted settings: RecentFiles (recently opened levels in
            Editor/.editor/recent.txt) and EditorPrefs (key = value preferences in Editor/.editor/prefs.txt).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <filesystem>
#include <string>
#include <utility>
#include <vector>


namespace vox::editor
{
	/**
	 * Recently opened levels, newest first, as asset-relative paths. Kept as one
	 * path per line in Editor/.editor/recent.txt beside the docking layout.
	 *
	 */
	class RecentFiles
	{
	public:

		/*!
			@brief
				Reads the list, replacing whatever was held.
			@param file
				Where the list is kept. An empty file disables persistence; the list then lives for
				the session only.
			*/
		void load(const std::filesystem::path& file);

		/*!
			@brief
				Moves a level to the front of the list, or adds it there, and saves the list.
			@param relative
				Asset-relative path of the level; an empty one is ignored
			*/
		void push(const std::string& relative);

		/*!
			@brief
				Takes a level off the list and saves the list.
			@param relative
				Asset-relative path of the level
			*/
		void remove(const std::string& relative);

		/*!
			@brief
				Returns the recent levels.
			@return
				Asset-relative paths, newest first
			*/
		const std::vector<std::string>&	items() const	{ return items_; }

	private:

		/*!
			@brief
				Writes the list to file_, one path per line; does nothing without a file.
			*/
		void save() const;

		std::filesystem::path		file_;
		std::vector<std::string>	items_;
	};

	/**
	 * The editor's own preferences - which panels are open, the frame cap - as
	 * `key = value` lines in Editor/.editor/prefs.txt. Unknown keys are kept, so
	 * an older build does not strip what a newer one wrote.
	 *
	 */
	class EditorPrefs
	{
	public:

		/*!
			@brief
				Reads the file, replacing whatever was held.
			@param file
				Where the preferences are kept. An empty file disables persistence.
			*/
		void load(const std::filesystem::path& file);

		/*!
			@brief
				Writes the file if a value changed since the last load or save.
			*/
		void save();

		/*!
			@brief
				Reads a boolean preference.
			@param key
				Name of the preference
			@param fallback
				Returned when the key is absent or is not a boolean
			@return
				The stored value, or fallback
			*/
		bool getBool(const std::string& key, bool fallback) const;

		/*!
			@brief
				Sets a value; save() writes it.
			@param key
				Name of the preference
			@param value
				Value to store
			*/
		void setBool(const std::string& key, bool value);

	private:

		std::filesystem::path								file_;
		std::vector<std::pair<std::string, std::string>>	values_;	// in file order
		bool												changed_	{};
	};
}
