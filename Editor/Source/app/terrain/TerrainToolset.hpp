/*************************************************************************************************************
\file       TerrainToolset.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The Terrain panel's Edit tab: owns the terrain tools and their shared brush settings, draws
            the tool grid and forwards viewport input to the selected tool.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Editor
#include "BrushSettings.hpp"
#include "TerrainTool.hpp"

// stds
#include <memory>
#include <string>
#include <vector>


namespace vox::editor
{
	/**
	 * The Terrain panel's Edit tab: the grid of tools, the selected tool's settings, and the one
	 * place the viewport hands the pointer to. Owns the tools and the brush settings they share.
	 *
	 * To add a tool: write a TerrainTool and construct it here. A new brush is less than that -
	 * one row in BrushTool's descriptor table.
	 *
	 */
	class TerrainToolset
	{
	public:

		/*!
			@brief
				Builds the Transform tool and one brush tool per descriptor row; none is selected.
			*/
		TerrainToolset();

		/*!
			@brief
				Destroys the tools (defined out of line so the tool types stay incomplete here).
			*/
		~TerrainToolset();

		/*!
			@brief
				Draws the tool grid and the selected tool's settings. Between ImGui::Begin and End.
			@param context
				What the tools may reach in the editor.
			@param unavailable
				Why terrain cannot be edited right now, or null when it can; shown instead of the settings.
			*/
		void drawPanel(ToolContext& context, const char* unavailable);

		/*!
			@brief
				Lets the selected tool read the pointer and queue its edits.
			@param input
				The pointer and keys over the scene view this frame.
			@param context
				What the tools may reach in the editor.
			*/
		void update(const ViewportInput& input, ToolContext& context);

		/*!
			@brief
				Lets the selected tool queue its cursor or gizmo.
			@param overlay
				The overlay renderer to queue shapes on.
			@param input
				The pointer and keys over the scene view this frame.
			@param context
				What the tools may reach in the editor.
			*/
		void drawOverlay(
			graphics::renderer::OverlayRenderer& overlay,
			const ViewportInput& input,
			ToolContext& context);

		/*!
			@brief
				Whether a tool is selected, so the left button over the viewport is the editor's.
			@return
				True when a tool is selected.
			*/
		bool hasTool() const { return selected_ >= 0; }

		/*!
			@brief
				Whether the selected tool is mid-stroke or mid-transform, so undo has to wait.
			@return
				True when the selected tool is busy.
			*/
		bool busy() const;

		/*!
			@brief
				Drops whatever the selected tool has open; the selection stays.
			@param context
				What the tools may reach in the editor.
			*/
		void deactivate(ToolContext& context);

		/*!
			@brief
				Everything the tools can check about themselves without a window.
			@return
				Empty when it all passes, else what failed.
			*/
		static std::string selfCheck();

	private:

		BrushSettings								brush_;
		std::vector<std::unique_ptr<TerrainTool>>	tools_;
		int											selected_	{ -1 };
	};
}
