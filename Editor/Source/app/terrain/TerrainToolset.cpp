/*************************************************************************************************************
\file       TerrainToolset.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Builds the terrain tools, draws the Edit tab's tool grid and settings, and forwards input,
            overlay drawing and deactivation to the selected tool.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "TerrainToolset.hpp"

// Editor
#include "BrushStroke.hpp"
#include "BrushTool.hpp"
#include "ToolWidgets.hpp"
#include "TransformTool.hpp"

// ImGui
#include <imgui.h>

// stds
#include <algorithm>


namespace vox::editor
{
	namespace
	{
		/*!
			@brief
				Draws one tool button of the grid: its icon with the name beneath.

				A TerrainTool's icon() takes a draw list, so the grid draws the button itself.
			@param tool
				The tool the button stands for.
			@param selected
				Whether to draw the button as selected.
			@param size
				Size of the button in pixels.
			@return
				Whether the button was clicked.
			*/
		bool toolButton(const TerrainTool& tool, bool selected, const ImVec2& size)
		{
			ImGui::PushID(tool.name());
			const ImVec2 at = ImGui::GetCursorScreenPos();
			if (selected)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			const bool clicked = ImGui::Button("##tool", size);
			if (selected)
				ImGui::PopStyleColor();
			widgets::tooltip(tool.tip());

			ImDrawList& draw = *ImGui::GetWindowDrawList();
			const ImU32 colour = ImGui::GetColorU32(ImGuiCol_Text);
			const float textHeight = ImGui::GetTextLineHeight() + 3.f;
			const float side = std::min(size.x, size.y - textHeight) - 8.f;
			const ImVec2 iconMin{ at.x + 0.5f * (size.x - side), at.y + 5.f };
			tool.icon(draw, iconMin, { iconMin.x + side, iconMin.y + side }, colour);
			const ImVec2 extent = ImGui::CalcTextSize(tool.name());
			draw.AddText(
				{ at.x + 0.5f * (size.x - extent.x), at.y + size.y - textHeight },
				colour,
				tool.name());
			ImGui::PopID();
			return clicked;
		}
	}

	/*!
		@brief
			Builds the Transform tool and one brush tool per descriptor row; none is selected.
		*/
	TerrainToolset::TerrainToolset()
	{
		tools_.push_back(std::make_unique<TransformTool>());
		for (const BrushToolDesc& desc : BrushTool::descriptors())
			tools_.push_back(std::make_unique<BrushTool>(desc, brush_));
	}

	TerrainToolset::~TerrainToolset() = default;

	/*!
		@brief
			Draws the tool grid and the selected tool's settings. Between ImGui::Begin and End.

			Clicking the selected tool puts it down.
		@param context
			What the tools may reach in the editor.
		@param unavailable
			Why terrain cannot be edited right now, or null when it can; shown instead of the settings.
		*/
	void TerrainToolset::drawPanel(ToolContext& context, const char* unavailable)
	{
		constexpr int kColumns = 3;
		const float gap = 2.f;
		const float width = (ImGui::GetContentRegionAvail().x - gap * (kColumns - 1)) / kColumns;

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
		for (int i = 0; i < static_cast<int>(tools_.size()); ++i)
		{
			if (i % kColumns != 0)
				ImGui::SameLine();
			if (toolButton(*tools_[i], i == selected_, { width, 62.f }))
			{
				// Clicking the selected tool puts it down: the viewport is the camera's again.
				if (selected_ >= 0)
					tools_[selected_]->deactivate(context);
				selected_ = i == selected_ ? -1 : i;
			}
		}
		ImGui::PopStyleVar();
		ImGui::Spacing();

		if (unavailable)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", unavailable);
			ImGui::PopStyleColor();
			return;
		}

		if (selected_ < 0)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("Choose a tool, then work on the terrain in the viewport. The right mouse "
			                   "button still looks around. "
			                   "Every stroke can be undone (Ctrl+Z).");
			ImGui::PopStyleColor();
			return;
		}

		tools_[selected_]->drawSettings(context);
	}

	/*!
		@brief
			Lets the selected tool read the pointer and queue its edits.
		@param input
			The pointer and keys over the scene view this frame.
		@param context
			What the tools may reach in the editor.
		*/
	void TerrainToolset::update(const ViewportInput& input, ToolContext& context)
	{
		if (selected_ >= 0)
			tools_[selected_]->update(input, context);
	}

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
	void TerrainToolset::drawOverlay(
		graphics::renderer::OverlayRenderer& overlay,
		const ViewportInput& input,
		ToolContext& context)
	{
		if (selected_ >= 0)
			tools_[selected_]->drawOverlay(overlay, input, context);
	}

	/*!
		@brief
			Whether the selected tool is mid-stroke or mid-transform, so undo has to wait.
		@return
			True when the selected tool is busy.
		*/
	bool TerrainToolset::busy() const
	{
		return selected_ >= 0 && tools_[selected_]->busy();
	}

	/*!
		@brief
			Drops whatever the selected tool has open; the selection stays.
		@param context
			What the tools may reach in the editor.
		*/
	void TerrainToolset::deactivate(ToolContext& context)
	{
		if (selected_ >= 0)
			tools_[selected_]->deactivate(context);
	}

	/*!
		@brief
			Runs the brush stroke checks, then the oriented box gizmo checks.
		@return
			Empty when it all passes, else the first failure.
		*/
	std::string TerrainToolset::selfCheck()
	{
		if (std::string fault = BrushStroke::selfCheck(); !fault.empty())
			return fault;
		return OrientedBoxGizmo::selfCheck();
	}
}
