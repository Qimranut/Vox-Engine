/*************************************************************************************************************
\file       EditorSettings.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements RecentFiles (a capped, de-duplicated most-recent-first level list) and EditorPrefs
            (key = value preference lines that keep unknown keys), both saved as plain text files.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "EditorSettings.hpp"

// stds
#include <algorithm>
#include <fstream>


namespace vox::editor
{
	namespace
	{
		// The list keeps this many levels; older ones fall off the end.
		constexpr size_t kMaxRecent = 8;
	}

	/*!
		@brief
			Reads the list, replacing whatever was held; blank and duplicate lines are skipped and at
			most kMaxRecent entries are kept.
		@param file
			Where the list is kept; empty disables persistence
		*/
	void RecentFiles::load(const std::filesystem::path& file)
	{
		file_ = file;
		items_.clear();

		if (file_.empty())
			return;

		std::ifstream in(file_);
		for (std::string line; std::getline(in, line) && items_.size() < kMaxRecent;)
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			if (!line.empty() && std::find(items_.begin(), items_.end(), line) == items_.end())
				items_.push_back(line);
		}
	}

	/*!
		@brief
			Moves a level to the front of the list, or adds it there, trims the list and saves it.
		@param relative
			Asset-relative path of the level; an empty one is ignored
		*/
	void RecentFiles::push(const std::string& relative)
	{
		if (relative.empty())
			return;

		items_.erase(std::remove(items_.begin(), items_.end(), relative), items_.end());
		items_.insert(items_.begin(), relative);

		if (items_.size() > kMaxRecent)
			items_.resize(kMaxRecent);

		save();
	}

	/*!
		@brief
			Takes a level off the list and saves the list.
		@param relative
			Asset-relative path of the level
		*/
	void RecentFiles::remove(const std::string& relative)
	{
		items_.erase(std::remove(items_.begin(), items_.end(), relative), items_.end());
		save();
	}

	/*!
		@brief
			Writes the list to file_, one path per line; does nothing without a file.
		*/
	void RecentFiles::save() const
	{
		if (file_.empty())
			return;

		// Losing the recent list is never worth failing the action that caused it.
		std::ofstream out(file_, std::ios::trunc);
		for (const auto& item : items_)
			out << item << '\n';
	}

	/*!
		@brief
			Reads the file's key = value lines, trimmed, replacing whatever was held.
		@param file
			Where the preferences are kept; empty disables persistence
		*/
	void EditorPrefs::load(const std::filesystem::path& file)
	{
		file_ = file;
		values_.clear();
		changed_ = false;

		if (file_.empty())
			return;

		const auto trim = [](std::string text)
		{
			const auto blank = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r'; };

			while (!text.empty() && blank(static_cast<unsigned char>(text.back())))
				text.pop_back();

			size_t first = 0;
			while (first < text.size() && blank(static_cast<unsigned char>(text[first])))
				++first;

			return text.substr(first);
		};

		std::ifstream in(file_);
		for (std::string line; std::getline(in, line);)
		{
			const size_t equals = line.find('=');
			if (equals == std::string::npos)
				continue;

			std::string key = trim(line.substr(0, equals));
			if (!key.empty())
				values_.emplace_back(std::move(key), trim(line.substr(equals + 1)));
		}
	}

	/*!
		@brief
			Writes every value in file order if one changed since the last load or save.
		*/
	void EditorPrefs::save()
	{
		if (file_.empty() || !changed_)
			return;

		// Like the recent list: losing a preference is never worth failing over.
		std::ofstream out(file_, std::ios::trunc);
		for (const auto& [key, value] : values_)
			out << key << " = " << value << '\n';

		changed_ = false;
	}

	/*!
		@brief
			Reads a boolean preference stored as "true" or "false".
		@param key
			Name of the preference
		@param fallback
			Returned when the key is absent or is not a boolean
		@return
			The stored value, or fallback
		*/
	bool EditorPrefs::getBool(const std::string& key, bool fallback) const
	{
		for (const auto& [name, value] : values_)
		{
			if (name != key)
				continue;

			if (value == "true")
				return true;
			if (value == "false")
				return false;

			break;
		}

		return fallback;
	}

	/*!
		@brief
			Sets a boolean preference, adding the key if absent; marks the prefs changed only when the
			value differs.
		@param key
			Name of the preference
		@param value
			Value to store
		*/
	void EditorPrefs::setBool(const std::string& key, bool value)
	{
		const char* text = value ? "true" : "false";

		for (auto& [name, held] : values_)
		{
			if (name == key)
			{
				if (held != text)
				{
					held = text;
					changed_ = true;
				}
				return;
			}
		}

		values_.emplace_back(key, text);
		changed_ = true;
	}
}
