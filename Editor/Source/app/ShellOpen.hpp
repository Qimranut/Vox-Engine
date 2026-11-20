/*************************************************************************************************************
\file       ShellOpen.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares openInTextEditor, which hands a file to the Windows shell so the user can edit it
            in the program associated with it, or in Notepad.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <filesystem>


namespace vox::editor
{
	/*!
		@brief
			Opens a text file in whatever the system opens that kind of file with.

			Falls back to Notepad when the extension has no program of its own - which is the usual
			case for the engine's formats.
		@param file
			File to open; made absolute before it is handed to the shell
		@return
			Whether something was started
		*/
	bool openInTextEditor(const std::filesystem::path& file);
}
