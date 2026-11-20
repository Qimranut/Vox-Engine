/*************************************************************************************************************
\file       ToolWidgets.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The terrain tools' shared ImGui controls and their icons, drawn with draw-list primitives.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "ToolWidgets.hpp"

// stds
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>


namespace vox::editor::widgets
{
	namespace
	{
		// Accent colour for the small mark that sets each icon apart from a plain ground line.
		constexpr ImU32 kAccent = IM_COL32(64, 128, 255, 255);

		// A point of the icon's square, in 0..1 on both axes with y down.
		struct Frame
		{
			ImVec2 min, size;

			/*!
				@brief
					Maps a point of the unit square to screen space.
				@param x
					0..1 across, left to right.
				@param y
					0..1 down, top to bottom.
				@return
					The screen position.
				*/
			ImVec2 at(float x, float y) const { return { min.x + size.x * x, min.y + size.y * y }; }

			/*!
				@brief
					Scales a length given as a share of the icon's smaller side.
				@param v
					The share of the smaller side.
				@return
					The length in pixels.
				*/
			float scale(float v) const { return v * std::min(size.x, size.y); }
		};

		/*!
			@brief
				Draws a rolling line of ground from one side of the icon to the other.
			@param draw
				The draw list to draw into.
			@param f
				The icon's square.
			@param colour
				Packed colour of the line.
			@param thickness
				Line thickness in pixels.
			@param lift
				How far to raise the line, as a share of the square (negative lowers it).
			*/
		void ground(ImDrawList& draw, const Frame& f, ImU32 colour, float thickness, float lift = 0.f)
		{
			draw.AddBezierCubic(
				f.at(0.06f, 0.78f - lift),
				f.at(0.30f, 0.20f - lift),
				f.at(0.50f, 0.95f - lift),
				f.at(0.94f, 0.52f - lift),
				colour,
				thickness);
		}
	}

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
	void drawIcon(ImDrawList& draw, Icon icon, const ImVec2& min, const ImVec2& max, ImU32 colour)
	{
		const Frame f{ min, { max.x - min.x, max.y - min.y } };
		const float t = std::max(1.5f, f.scale(0.07f));
		const ImU32 faint = (colour & 0x00FFFFFFu) | (static_cast<ImU32>((colour >> 24) * 0.45f) << 24);

		switch (icon)
		{
			case Icon::Transform:
			{
				draw.AddRect(f.at(0.22f, 0.22f), f.at(0.78f, 0.78f), colour, 0.f, 0, t);
				const float h = f.scale(0.08f);
				for (const ImVec2& c :
				     { f.at(0.22f, 0.22f), f.at(0.78f, 0.22f), f.at(0.22f, 0.78f), f.at(0.78f, 0.78f) })
					draw.AddRectFilled({ c.x - h, c.y - h }, { c.x + h, c.y + h }, colour);
				break;
			}

			case Icon::Draw:
				ground(draw, f, colour, t);
				draw.AddLine(f.at(0.72f, 0.10f), f.at(0.72f, 0.38f), kAccent, t);
				draw.AddLine(f.at(0.58f, 0.24f), f.at(0.86f, 0.24f), kAccent, t);
				break;

			case Icon::Sculpt:
				ground(draw, f, colour, t);
				draw.AddLine(f.at(0.70f, 0.44f), f.at(0.70f, 0.10f), kAccent, t);
				draw.AddLine(f.at(0.58f, 0.22f), f.at(0.70f, 0.10f), kAccent, t);
				draw.AddLine(f.at(0.82f, 0.22f), f.at(0.70f, 0.10f), kAccent, t);
				break;

			case Icon::Smooth:
				ground(draw, f, faint, t);
				draw.AddBezierCubic(
					f.at(0.06f, 0.62f),
					f.at(0.36f, 0.42f),
					f.at(0.60f, 0.70f),
					f.at(0.94f, 0.50f),
					colour,
					t);
				break;

			case Icon::Paint:
				ground(draw, f, colour, t, -0.10f);
				draw.AddLine(f.at(0.84f, 0.08f), f.at(0.56f, 0.40f), colour, t * 1.4f);
				draw.AddTriangleFilled(f.at(0.60f, 0.34f), f.at(0.50f, 0.56f), f.at(0.40f, 0.44f), kAccent);
				break;

			case Icon::Flatten:
				ground(draw, f, faint, t);
				draw.AddLine(f.at(0.06f, 0.56f), f.at(0.94f, 0.56f), kAccent, t);
				break;

			case Icon::Erode:
				ground(draw, f, colour, t, -0.12f);
				for (const ImVec2& c : { f.at(0.30f, 0.20f), f.at(0.54f, 0.12f), f.at(0.76f, 0.26f) })
					draw.AddCircleFilled(c, f.scale(0.055f), kAccent);
				break;

			case Icon::Sphere:
				draw.AddCircle(f.at(0.5f, 0.5f), f.scale(0.34f), colour, 0, t);
				draw.AddEllipse(
					f.at(0.5f, 0.5f),
					{ f.scale(0.34f), f.scale(0.12f) },
					faint,
					0.f,
					0,
					t * 0.8f);
				break;

			case Icon::Cube:
				draw.AddRect(f.at(0.20f, 0.36f), f.at(0.66f, 0.82f), colour, 0.f, 0, t);
				draw.AddLine(f.at(0.20f, 0.36f), f.at(0.36f, 0.20f), colour, t);
				draw.AddLine(f.at(0.66f, 0.36f), f.at(0.82f, 0.20f), colour, t);
				draw.AddLine(f.at(0.66f, 0.82f), f.at(0.82f, 0.66f), colour, t);
				draw.AddLine(f.at(0.36f, 0.20f), f.at(0.82f, 0.20f), colour, t);
				draw.AddLine(f.at(0.82f, 0.20f), f.at(0.82f, 0.66f), colour, t);
				break;

			case Icon::Cylinder:
				draw.AddEllipse(f.at(0.5f, 0.28f), { f.scale(0.28f), f.scale(0.10f) }, colour, 0.f, 0, t);
				draw.AddLine(f.at(0.22f, 0.28f), f.at(0.22f, 0.72f), colour, t);
				draw.AddLine(f.at(0.78f, 0.28f), f.at(0.78f, 0.72f), colour, t);
				draw.PathArcTo(f.at(0.5f, 0.72f), f.scale(0.28f), 0.f, 3.14159265f);
				draw.PathStroke(colour, 0, t);
				break;

			case Icon::FlattenBoth:
			case Icon::FlattenLower:
			case Icon::FlattenRaise:
			{
				// A bump above the plane and a pit below it; the part the mode works on is solid.
				const bool lower = icon != Icon::FlattenRaise, raise = icon != Icon::FlattenLower;
				draw.AddLine(f.at(0.06f, 0.5f), f.at(0.94f, 0.5f), kAccent, t);
				draw.AddBezierCubic(
					f.at(0.12f, 0.5f),
					f.at(0.20f, 0.06f),
					f.at(0.40f, 0.06f),
					f.at(0.48f, 0.5f),
					lower ? colour : faint,
					t);
				draw.AddBezierCubic(
					f.at(0.52f, 0.5f),
					f.at(0.60f, 0.94f),
					f.at(0.80f, 0.94f),
					f.at(0.88f, 0.5f),
					raise ? colour : faint,
					t);
				break;
			}
		}
	}

	/*!
		@brief
			Shows text as the last item's tooltip, once hovered a moment.
		@param text
			The tooltip text; null shows nothing.
		*/
	void tooltip(const char* text)
	{
		if (text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", text);
	}

	/*!
		@brief
			A caption above the control that follows, after a little spacing.
		@param text
			The caption text.
		*/
	void caption(const char* text)
	{
		ImGui::Spacing();
		ImGui::TextUnformatted(text);
	}

	/*!
		@brief
			A button that is an icon with an optional caption under it.
		@param id
			ImGui id of the button.
		@param icon
			The icon to draw on it.
		@param text
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
		const char* text,
		bool selected,
		const ImVec2& size,
		const char* tip)
	{
		ImGui::PushID(id);
		const ImVec2 at = ImGui::GetCursorScreenPos();
		if (selected)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		const bool clicked = ImGui::Button("##icon", size);
		if (selected)
			ImGui::PopStyleColor();
		tooltip(tip);

		ImDrawList& draw = *ImGui::GetWindowDrawList();
		const ImU32 colour = ImGui::GetColorU32(ImGuiCol_Text);
		const float textHeight = text ? ImGui::GetTextLineHeight() + 2.f : 0.f;
		const float side = std::min(size.x, size.y - textHeight) - 8.f;
		const ImVec2 iconMin{ at.x + 0.5f * (size.x - side), at.y + 4.f };
		drawIcon(draw, icon, iconMin, { iconMin.x + side, iconMin.y + side }, colour);
		if (text)
		{
			const ImVec2 extent = ImGui::CalcTextSize(text);
			draw.AddText(
				{ at.x + 0.5f * (size.x - extent.x), at.y + size.y - textHeight - 1.f },
				colour,
				text);
		}
		ImGui::PopID();
		return clicked;
	}

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
	bool segmented(const char* id, int& value, std::initializer_list<const char*> labels)
	{
		ImGui::PushID(id);
		const float spacing = 1.f;
		const int count = static_cast<int>(labels.size());
		const float width = (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(count - 1))
		                    / static_cast<float>(count);
		bool changed = false;
		int i = 0;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, ImGui::GetStyle().ItemSpacing.y));
		for (const char* label : labels)
		{
			if (i > 0)
				ImGui::SameLine();
			const bool on = i == value;
			if (on)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			ImGui::PushID(i);
			if (ImGui::Button(label, ImVec2(width, 0)) && !on)
			{
				value = i;
				changed = true;
			}
			ImGui::PopID();
			if (on)
				ImGui::PopStyleColor();
			++i;
		}
		ImGui::PopStyleVar();
		ImGui::PopID();
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
	bool shapeButtons(voxel::edit::Shape& shape)
	{
		using voxel::edit::Shape;

		struct Entry
		{
			Shape shape;
			Icon icon;
			const char* id;
			const char* tip;
		};

		constexpr Entry kShapes[] = {
			{ Shape::Sphere, Icon::Sphere, "sphere", "Sphere" },
			{ Shape::Cube, Icon::Cube, "cube", "Cube" },
			{ Shape::Cylinder, Icon::Cylinder, "cylinder", "Cylinder, standing upright" }
		};
		const float spacing = 1.f;
		const float width = (ImGui::GetContentRegionAvail().x - 2.f * spacing) / 3.f;
		bool changed = false;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, ImGui::GetStyle().ItemSpacing.y));
		for (const Entry& e : kShapes)
		{
			if (e.shape != Shape::Sphere)
				ImGui::SameLine();
			if (iconButton(e.id, e.icon, nullptr, shape == e.shape, { width, 40.f }, e.tip)
			    && shape != e.shape)
			{
				shape = e.shape;
				changed = true;
			}
		}
		ImGui::PopStyleVar();
		return changed;
	}

	/*!
		@brief
			Every material but air as a swatch of its colour, the chosen one outlined.
		@param id
			ImGui id of the grid.
		@param materials
			The material table to list.
		@param selected
			The chosen material; set to the clicked one.
		@return
			Whether the choice changed.
		*/
	bool materialGrid(const char* id, const voxel::MaterialTable& materials, voxel::MaterialId& selected)
	{
		ImGui::PushID(id);
		constexpr float kSwatch = 38.f, kGap = 3.f;
		const int perRow =
			std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + kGap) / (kSwatch + kGap)));
		bool changed = false;
		int shown = 0;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kGap, kGap));
		for (std::size_t i = 1; i < materials.size(); ++i)
		{
			const voxel::Material& material = materials.get(static_cast<voxel::MaterialId>(i));
			if (shown++ % perRow != 0)
				ImGui::SameLine();
			ImGui::PushID(static_cast<int>(i));
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const glm::vec3& c = material.colourSrgb_;
			if (ImGui::ColorButton(
					"##m",
					ImVec4(c.r, c.g, c.b, 1.f),
					ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop
						| ImGuiColorEditFlags_NoBorder,
					ImVec2(kSwatch, kSwatch)))
			{
				changed = selected != i;
				selected = static_cast<voxel::MaterialId>(i);
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			{
				std::string name = material.name_;
				if (!name.empty())
					name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
				ImGui::SetTooltip("%s", name.c_str());
			}
			if (selected == i)
				ImGui::GetWindowDrawList()->AddRect(
					{ at.x - 1.f, at.y - 1.f },
					{ at.x + kSwatch + 1.f, at.y + kSwatch + 1.f },
					kAccent,
					3.f,
					0,
					2.5f);
			ImGui::PopID();
		}
		ImGui::PopStyleVar();
		ImGui::PopID();
		return changed;
	}
}
