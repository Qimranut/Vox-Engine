/*************************************************************************************************************
\file       ShellOpen.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements openInTextEditor with ShellExecute: the file's associated program first, then
            Notepad. windows.h is kept to this file because it defines names the Editor uses.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "ShellOpen.hpp"

// Windows
// WIN32_LEAN_AND_MEAN and NOMINMAX are defined project-wide. Kept to this file: windows.h
// defines names (near, far) the editor's own code uses.
#include <windows.h>
#include <shellapi.h>

// stds
#include <string>


namespace vox::editor
{
	/*!
		@brief
			Opens a file with its associated program, or in Notepad when the shell cannot.
		@param file
			File to open; made absolute and normalised before it is handed to the shell
		@return
			Whether something was started
		*/
	bool openInTextEditor(const std::filesystem::path& file)
	{
		std::error_code ec;
		std::filesystem::path full = std::filesystem::absolute(file, ec).lexically_normal();
		full.make_preferred();

		// ShellExecute reports success as a value above 32.
		auto result = reinterpret_cast<INT_PTR>(
			ShellExecuteW(nullptr, L"open", full.c_str(), nullptr, nullptr, SW_SHOWNORMAL));

		if (result > 32)
			return true;

		const std::wstring quoted = L"\"" + full.native() + L"\"";
		result = reinterpret_cast<INT_PTR>(
			ShellExecuteW(nullptr, L"open", L"notepad.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL));

		return result > 32;
	}
}
