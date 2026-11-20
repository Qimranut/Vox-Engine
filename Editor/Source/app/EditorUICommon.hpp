/*************************************************************************************************************
\file       EditorUICommon.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Small helpers shared by the EditorUI translation units (EditorUI*.cpp): status colours, the
            theme, ImGui buffer copying, case-insensitive filtering, popup detection and raw file reads.
            Not part of any panel's interface: nothing outside EditorUI includes this.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// ImGui
#include <imgui.h>

// stds
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>


namespace vox::editor
{
	// Status colours used wherever the editor warns or reports a failure.
	inline const ImVec4 kWarningText{ 1.00f, 0.80f, 0.35f, 1.f };
	inline const ImVec4 kErrorText{ 1.00f, 0.45f, 0.40f, 1.f };

	/*!
		@brief
			Applies the editor's look to the current ImGui context.

			Defined in EditorUIChrome.cpp.
		*/
	void applyTheme();

	/*!
		@brief
			Copies a string into one of the fixed buffers ImGui's text boxes edit, truncating what does
			not fit.
		@param to
			Destination buffer, always left null-terminated
		@param from
			Text to copy
		*/
	template<size_t N>
	void copy(char (&to)[N], const std::string& from)
	{
		std::snprintf(to, N, "%s", from.c_str());
	}

	/*!
		@brief
			Case-insensitive substring test, for filter boxes.
		@param text
			Text to search
		@param needle
			What to look for. Empty matches everything.
		@return
			Whether text contains needle
		*/
	inline bool containsNoCase(const std::string& text, const char* needle)
	{
		if (!needle[0])
			return true;

		const auto same = [](char a, char b)
		{
			return std::tolower(static_cast<unsigned char>(a))
				== std::tolower(static_cast<unsigned char>(b));
		};

		const std::string_view n(needle);
		return std::search(text.begin(), text.end(), n.begin(), n.end(), same) != text.end();
	}

	/*!
		@brief
			Tests whether any ImGui popup is open.
		@return
			Whether any popup or modal is open, at any level
		*/
	inline bool anyPopupOpen()
	{
		return ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
	}

	/*!
		@brief
			Reads a whole file from disk, bypassing the resource cache.

			A file edited in another program is therefore seen as it is now.
		@param file
			Absolute path
		@return
			The file's bytes, or nothing when it could not be opened
		*/
	inline std::optional<std::vector<unsigned char>> readFileBytes(const std::filesystem::path& file)
	{
		std::ifstream in(file, std::ios::binary);
		if (!in)
			return std::nullopt;

		return std::vector<unsigned char>(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}
}
