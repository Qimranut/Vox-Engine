/*************************************************************************************************************
\file       ToolWidgets.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      ImGui controls the terrain tools share that ImGui has no widget for: segmented buttons,
            icon buttons drawn with draw-list primitives, and a grid of material swatches.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "voxel/edit/brush.hpp"
#include "voxel/types/material_table.hpp"

// ImGui
#include <imgui.h>

// stds
#include <initializer_list>


/**
 * The few controls the terrain tools share that ImGui has no widget for: a row of mutually
 * exclusive buttons, icon buttons, and a grid of material swatches. Icons are drawn with
 * draw-list primitives, so the editor ships no icon font or image for them.
 *
 */
namespace vox::editor::widgets
{
	enum class Icon
	{
		Transform,
		Draw,
		Sculpt,
		Smooth,
		Paint,
		Flatten,
		Erode,
		Sphere,
		Cube,
		Cylinder,
		FlattenBoth,
		FlattenLower,
		FlattenRaise
	};

	/*!
		@brief
			Draws an icon into the square from min to max with draw-list primitives.
		@param draw
			The draw list to draw into.
		@param icon
			Which icon to draw.
		@param min
			Top-left corner of the square.
		@param max
			Bottom-right corner of the square.
		@param colour
			Packed colour for the main strokes; accents use the fixed accent colour.
		*/
	void drawIcon(ImDrawList& draw, Icon icon, const ImVec2& min, const ImVec2& max, ImU32 colour);

	/*!
		@brief
			A button that is an icon with an optional caption under it.
		@param id
			ImGui id of the button.
		@param icon
			The icon to draw on it.
		@param caption
			Text under the icon, or null for none.
		@param selected
			Whether to draw the button as selected.
		@param size
			Size of the button in pixels.
		@param tip
			Tooltip text, or null for none.
		@return
			Whether it was clicked.
		*/
	bool iconButton(
		const char* id,
		Icon icon,
		const char* caption,
		bool selected,
		const ImVec2& size,
		const char* tip);

	/*!
		@brief
			A caption above the control that follows, after a little spacing.
		@param text
			The caption text.
		*/
	void caption(const char* text);

	/*!
		@brief
			One row of equal buttons of which exactly one is on, filling the width.
		@param id
			ImGui id of the row.
		@param value
			Index of the button that is on; set to the clicked one.
		@param labels
			The buttons' labels, in order.
		@return
			Whether the choice changed.
		*/
	bool segmented(const char* id, int& value, std::initializer_list<const char*> labels);

	/*!
		@brief
			segmented() over an enum whose values count up from zero.
		@param id
			ImGui id of the row.
		@param value
			The enum value that is on; set to the clicked one.
		@param labels
			The buttons' labels, one per enum value in order.
		@return
			Whether the choice changed.
		*/
	template<typename E>
	bool segmentedEnum(const char* id, E& value, std::initializer_list<const char*> labels)
	{
		int v = static_cast<int>(value);
		const bool changed = segmented(id, v, labels);
		value = static_cast<E>(v);
		return changed;
	}

	/*!
		@brief
			The three brush shapes as icon buttons.
		@param shape
			The selected shape; set to the clicked one.
		@return
			Whether the shape changed.
		*/
	bool shapeButtons(voxel::edit::Shape& shape);

	/*!
		@brief
			Every material but air as a swatch of its colour, the chosen one outlined.

			New entries in materials.json appear here without a code change.
		@param id
			ImGui id of the grid.
		@param materials
			The material table to list.
		@param selected
			The chosen material; set to the clicked one.
		@return
			Whether the choice changed.
		*/
	bool materialGrid(const char* id, const voxel::MaterialTable& materials, voxel::MaterialId& selected);

	/*!
		@brief
			Shows text as the last item's tooltip, once hovered a moment.
		@param text
			The tooltip text; null shows nothing.
		*/
	void tooltip(const char* text);
}
