/*************************************************************************************************************
\file       main.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Entry point of the Editor: runs the engine with the EditorUI as its application layer and
            reports any exception that escapes.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

// Editor
#include "Source/app/EditorUI.hpp"

// stds
#include <iostream>


/*!
	@brief
		Starts the engine with the Editor layer on the "Assets" root, runs frames until a window
		closes, then shuts down.
	@return
		0 on a normal exit, 1 when an exception escaped.
	*/
int main()
{
	try
	{
		vox::editor::EditorUI editor;
		engine::VoxEngine engine;
		engine.init(&editor, "Assets");
		while (engine.update())
		{
		}
		engine.shutdown();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor error: " << error.what() << '\n';
		return 1;
	}
}
