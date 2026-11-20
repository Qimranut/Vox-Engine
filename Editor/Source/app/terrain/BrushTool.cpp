/*************************************************************************************************************
\file       BrushTool.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The brush tools' descriptor table, settings UI, per-frame stroke handling (aiming,
            picking, stamping into one voxel transaction) and overlay drawing.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "BrushTool.hpp"

// Graphics
#include "graphics/renderer/overlay_renderer.hpp"

// Voxel
#include "voxel/sys/voxel_sys.hpp"

// ImGui
#include <imgui.h>

// stds
#include <algorithm>
#include <cctype>
#include <cmath>


namespace vox::editor
{
	namespace
	{
		using graphics::renderer::OverlayRenderer;
		using voxel::edit::Op;
		using widgets::Icon;

		//                kind                 name       tip                                                          icon           undo label         mode   str    snap   lock   flat   mat    matMode
		constexpr BrushToolDesc kTools[] = {
			{ BrushKind::Draw,    "Draw",    "Add or remove terrain with the brush.\nCtrl: the other mode. Shift: smooth.",          Icon::Draw,    "Draw terrain",    true,  false, true,  true,  false, true,  false },
			{ BrushKind::Sculpt,  "Sculpt",  "Push the surface out or in, gradually.\nCtrl: the other mode. Shift: smooth.",         Icon::Sculpt,  "Sculpt terrain",  true,  true,  false, true,  false, true,  false },
			{ BrushKind::Smooth,  "Smooth",  "Wear down spikes and fill pits.",                                                      Icon::Smooth,  "Smooth terrain",  false, true,  true,  true,  false, false, false },
			{ BrushKind::Paint,   "Paint",   "Change the material of terrain, or replace one material with another.",                Icon::Paint,   "Paint terrain",   false, false, true,  true,  false, false, true },
			{ BrushKind::Flatten, "Flatten", "Bring terrain to a level plane.",                                                      Icon::Flatten, "Flatten terrain", false, true,  true,  false, true,  true,  false },
			{ BrushKind::Erode,   "Erode",   "Run water and gravity over the surface: gullies on slopes, scree under cliffs.",       Icon::Erode,   "Erode terrain",   false, true,  false, true,  false, false, false },
		};

		const glm::vec4 kAddColour{ 0.92f, 0.95f, 1.00f, 0.24f };
		const glm::vec4 kSubtractColour{ 1.00f, 0.42f, 0.36f, 0.26f };
		const glm::vec4 kSmoothColour{ 0.55f, 0.95f, 0.70f, 0.24f };
		const glm::vec4 kPlaneColour{ 0.30f, 0.56f, 1.00f, 0.30f };
		const glm::vec4 kGridColour{ 0.30f, 0.56f, 1.00f, 0.55f };
		const glm::vec4 kDotColour{ 1.f, 1.f, 1.f, 0.95f };

		/*!
			@brief
				How many metres one screen pixel covers at a point.
			@param point
				The point in world space.
			@param input
				The viewport input, for the eye, vertical fov and viewport height.
			@return
				World size of a pixel at that point's distance.
			*/
		float worldPerPixel(const glm::vec3& point, const ViewportInput& input)
		{
			const float distance = std::max(0.25f, glm::length(point - input.eye));
			return 2.f * distance * std::tan(0.5f * input.fovY) / std::max(1.f, input.viewportHeight);
		}

		/*!
			@brief
				Whether an op takes terrain away.
			@param op
				The engine op.
			@return
				True for Subtract and Shrink.
			*/
		bool removes(Op op)
		{
			return op == Op::Subtract || op == Op::Shrink;
		}

		/*!
			@brief
				Capitalises the first letter of a name.
			@param name
				The name, taken by value.
			@return
				The name with its first character upper-cased.
			*/
		std::string titled(std::string name)
		{
			if (!name.empty())
				name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
			return name;
		}
	}

	/*!
		@brief
			Every brush tool there is, in the order the panel lists them.
		@return
			The descriptor table.
		*/
	std::span<const BrushToolDesc> BrushTool::descriptors()
	{
		return kTools;
	}

	/*!
		@brief
			Draws the descriptor's icon into a square.
		@param draw
			The draw list to draw into.
		@param min
			Top-left corner of the square.
		@param max
			Bottom-right corner of the square.
		@param colour
			Packed ImGui colour to draw with.
		*/
	void BrushTool::icon(ImDrawList& draw, const ImVec2& min, const ImVec2& max, unsigned int colour) const
	{
		widgets::drawIcon(draw, desc_.icon, min, max, colour);
	}

	/*!
		@brief
			Draws the brush, material and filter settings this tool uses.
		@param context
			What the tool may reach in the editor.
		*/
	void BrushTool::drawSettings(ToolContext& context)
	{
		using namespace widgets;
		BrushSettings& s = settings_;
		const voxel::MaterialTable& materials = context.voxel.materials();

		if (ImGui::CollapsingHeader("Brush Settings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (desc_.mode)
			{
				caption("Brush Mode");
				segmentedEnum("mode", s.mode, { "Add", "Subtract" });
				tooltip("Hold Ctrl in the viewport for the other mode.");
			}
			caption("Brush Shape");
			shapeButtons(s.shape);

			if (desc_.flatten)
			{
				caption("Flatten Mode");

				struct Entry
				{
					voxel::edit::FlattenMode mode;
					Icon icon;
					const char* id;
					const char* tip;
				};

				constexpr Entry kModes[] = { { voxel::edit::FlattenMode::Lower,
					                           Icon::FlattenLower,
					                           "lower",
					                           "Only remove what stands above the plane" },
					                         { voxel::edit::FlattenMode::Raise,
					                           Icon::FlattenRaise,
					                           "raise",
					                           "Only fill what lies below the plane" },
					                         { voxel::edit::FlattenMode::Both,
					                           Icon::FlattenBoth,
					                           "both",
					                           "Lower what is above and raise what is below" } };
				const float width = (ImGui::GetContentRegionAvail().x - 2.f) / 3.f;
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.f, ImGui::GetStyle().ItemSpacing.y));
				for (const Entry& e : kModes)
				{
					if (e.mode != voxel::edit::FlattenMode::Lower)
						ImGui::SameLine();
					if (iconButton(e.id, e.icon, nullptr, s.flattenMode == e.mode, { width, 40.f }, e.tip))
						s.flattenMode = e.mode;
				}
				ImGui::PopStyleVar();
			}

			caption("Brush Size");
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat(
					"##size",
					&s.size,
					BrushSettings::kMinSize,
					BrushSettings::kMaxSize,
					"%.1f m",
					ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)
			    && s.linkHeight)
				s.height = s.size;
			tooltip("Across the brush. Hold B and scroll in the viewport.");
			if (s.shape != voxel::edit::Shape::Sphere)
			{
				caption("Brush Height");
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderFloat(
						"##height",
						&s.height,
						BrushSettings::kMinSize,
						BrushSettings::kMaxSize,
						"%.1f m",
						ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
					s.linkHeight = false;
				tooltip("Hold Ctrl+B and scroll in the viewport.");
				if (!s.linkHeight)
				{
					if (ImGui::SmallButton("Match size"))
					{
						s.linkHeight = true;
						s.height = s.size;
					}
				}
			}
			if (desc_.strength)
			{
				caption("Strength");
				ImGui::SetNextItemWidth(-1);
				ImGui::SliderFloat(
					"##strength",
					&s.strength,
					0.05f,
					1.f,
					"%.2f",
					ImGuiSliderFlags_AlwaysClamp);
				tooltip("How much one pass changes. Hold Shift+B and scroll in the viewport.");
			}

			caption("Pivot Position");
			segmentedEnum("pivot", s.pivot, { "Bottom", "Center", "Top" });
			tooltip("Which part of the brush sits on the point under the pointer.");

			if (desc_.snapping)
			{
				caption("Snapping");
				segmentedEnum("snapping", s.snapping, { "Off", "Voxel" });
			}
			if (desc_.planeLock)
			{
				caption("Plane Lock");
				segmentedEnum("lock", s.planeLock, { "Off", "Auto", "Manual" });
				tooltip("Off: the brush follows the terrain.\nAuto: a stroke stays on the face it began "
				        "on.\nManual: the brush stays on a level plane you place.");
				if (s.planeLock == PlaneLock::Manual)
				{
					ImGui::SetNextItemWidth(-1);
					ImGui::DragFloat(
						"##planeHeight",
						&s.manualPlaneHeight,
						0.1f,
						0.f,
						0.f,
						"Plane at %.1f m");
				}
			}
			if (desc_.flatten)
			{
				caption("Flatten Plane");
				segmentedEnum("flattenPlane", s.flattenPlane, { "Auto", "Fixed" });
				tooltip("Auto: the height where the stroke begins.\nFixed: a height you type.");
				if (s.flattenPlane == FlattenPlane::Fixed)
				{
					ImGui::SetNextItemWidth(-1);
					ImGui::DragFloat("##fixedHeight", &s.fixedPlaneHeight, 0.1f, 0.f, 0.f, "Plane at %.1f m");
				}
			}
			ImGui::Spacing();
			ImGui::Checkbox("Ignore Water", &s.ignoreWater);
			tooltip("The brush passes through water and works on the bed beneath it.");
		}

		// ---------------------------- Material Settings -----------------------------

		if ((desc_.material || desc_.materialMode)
		    && ImGui::CollapsingHeader("Material Settings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (desc_.materialMode)
			{
				caption("Material Mode");
				segmentedEnum("materialMode", s.materialMode, { "Paint", "Replace" });
				if (s.materialMode == MaterialMode::Replace)
				{
					caption("Source Material");
					materialGrid("source", materials, s.source);
					caption("Target Material");
				}
				else
					caption("Material");
				materialGrid("material", materials, s.material);
			}
			else
			{
				ImGui::Checkbox("Auto Material", &s.autoMaterial);
				tooltip("New terrain takes the material of the ground it grows out of.");
				ImGui::BeginDisabled(s.autoMaterial);
				caption("Source Material");
				materialGrid("material", materials, s.material);
				ImGui::EndDisabled();
			}
			ImGui::TextDisabled(
				"%s  -  Alt+click terrain to pick",
				titled(materials.get(s.material).name_).c_str());
		}

		// -------------------------------- Filters ------------------------------------

		if (ImGui::CollapsingHeader("Filters"))
		{
			ImGui::TextDisabled("Limit the brush to some of the ground it covers.");
			ImGui::Checkbox("By slope", &s.filterSlope);
			if (s.filterSlope)
			{
				ImGui::SetNextItemWidth(-1);
				ImGui::DragFloatRange2(
					"##slope",
					&s.slopeMin,
					&s.slopeMax,
					0.5f,
					0.f,
					90.f,
					"from %.0f deg",
					"to %.0f deg",
					ImGuiSliderFlags_AlwaysClamp);
				tooltip("0 is level ground, 90 a wall. Paint 45-90 to put rock on cliffs only.");
			}
			ImGui::Checkbox("By height", &s.filterHeight);
			if (s.filterHeight)
			{
				ImGui::SetNextItemWidth(-1);
				ImGui::DragFloatRange2(
					"##height",
					&s.heightMin,
					&s.heightMax,
					0.1f,
					0.f,
					0.f,
					"from %.1f m",
					"to %.1f m");
			}
			ImGui::Checkbox("By material", &s.filterMaterial);
			if (s.filterMaterial)
				materialGrid("filter", materials, s.filterMaterialId);
		}
	}

	/*!
		@brief
			Resizes the brush, its height (Ctrl) or strength (Shift) with the wheel while the adjust key is held.
		@param input
			The pointer and keys over the scene view this frame.
		*/
	void BrushTool::adjustWithWheel(const ViewportInput& input)
	{
		if (!input.adjust || input.wheel == 0.f)
			return;
		BrushSettings& s = settings_;
		if (input.shift)
			s.strength = std::clamp(s.strength + 0.05f * input.wheel, 0.05f, 1.f);
		else if (input.ctrl)
		{
			s.height = std::clamp(
				s.height * std::pow(1.1f, input.wheel),
				BrushSettings::kMinSize,
				BrushSettings::kMaxSize);
			s.linkHeight = false;
		}
		else
		{
			s.size = std::clamp(
				s.size * std::pow(1.1f, input.wheel),
				BrushSettings::kMinSize,
				BrushSettings::kMaxSize);
			if (s.linkHeight)
				s.height = s.size;
		}
	}

	/*!
		@brief
			Ends the open transaction, if any, and hands it to the editor's undo stack.
		@param context
			What the tool may reach in the editor.
		*/
	void BrushTool::finish(ToolContext& context)
	{
		if (!transaction_)
			return;
		context.voxel.endEdit();
		if (context.committed)
			context.committed(transaction_, desc_.undoLabel);
		transaction_ = 0;
	}

	/*!
		@brief
			Cancels the stroke, clears the cursor and closes any open transaction.
		@param context
			What the tool may reach in the editor.
		*/
	void BrushTool::deactivate(ToolContext& context)
	{
		stroke_.cancel();
		cursor_ = {};
		finish(context);
	}

	/*!
		@brief
			Aims the brush, handles Alt+click picking and wheel sizing, and queues the stroke's stamps.

			A stroke opens a voxel transaction when it begins and closes it when it ends.
		@param input
			The pointer and keys over the scene view this frame.
		@param context
			What the tool may reach in the editor.
		*/
	void BrushTool::update(const ViewportInput& input, ToolContext& context)
	{
		voxel::System& vox = context.voxel;
		const voxel::World* world = vox.world();
		if (!world || !vox.editable())
		{
			deactivate(context);
			return;
		}

		adjustWithWheel(input);

		const voxel::WorldDims& dims = world->dims();
		voxel::edit::RaycastOptions options;
		if (settings_.ignoreWater)
		{
			const voxel::MaterialId water = vox.materials().id("water");
			if (water != voxel::kUnknownMaterial)
				options.passThrough_ = water;
		}
		// For as long as a stroke lasts the brush aims at the terrain the stroke began on. Aimed at
		// the live terrain, Draw hits the blob it has just added - which is nearer the camera - and
		// the next stamp lands on that, and the stroke shoots towards the eye; Subtract burrows the
		// same way. Between strokes it aims at what is there.
		const std::optional<voxel::edit::VoxelHit> hit = vox.raycast(input.ray, options, stroke_.active());

		// Alt+click picks the material under the pointer instead of starting a stroke.
		const bool picking = input.alt && !stroke_.active();
		if (picking && input.leftPressed && input.over && hit)
		{
			(desc_.materialMode && settings_.materialMode == MaterialMode::Replace ? settings_.source
			                                                                       : settings_.material) =
				hit->material_;
			settings_.autoMaterial = false;
			if (context.log)
				context.log(LogLevel::Info, "Picked " + vox.materials().get(hit->material_).name_ + ".");
		}

		voxel::edit::OpParams params =
			resolveOp(desc_.kind, settings_, input.ctrl, input.shift, dims.voxelSize_, dims.origin_.y);
		shownOp_ = params.op_;

		BrushStroke::Input in;
		in.ray = input.ray;
		if (hit)
			in.hit = SurfaceHit{ hit->point_, glm::vec3(hit->normal_), hit->material_ };
		in.over = input.over;
		in.down = input.leftDown;
		in.pressed = input.leftPressed && !picking;
		in.time = input.time;

		// A brush held still only repeats once the last stamp has landed, or a slow machine
		// would queue them faster than it applies them.
		const bool repeats = repeatsWhileHeld(params.op_) && !vox.editsPending();
		const BrushStroke::Frame frame =
			stroke_.update(settings_, in, dims.voxelSize_, dims.origin_, repeats);
		cursor_ = frame.cursor;

		if (frame.began)
		{
			transaction_ = vox.beginEdit(desc_.undoLabel);
			flattenHeight_ = settings_.flattenPlane == FlattenPlane::Fixed ? settings_.fixedPlaneHeight
			                                                               : stroke_.startPoint().y;
		}
		if (transaction_ && !frame.stamps.empty())
		{
			params.planeY_ = (flattenHeight_ - dims.origin_.y) / dims.voxelSize_;
			for (const glm::vec3& centre : frame.stamps)
			{
				params.seed_ = 0x9E3779B97F4A7C15ull * ++stamps_;
				vox.queueBrush(
					{ voxel::edit::stampFromWorld(
						dims,
						centre,
						settings_.radius(),
						settings_.halfHeight(),
						settings_.shape) },
					params);
			}
		}
		if (frame.ended)
			finish(context);
	}

	/*!
		@brief
			Queues the brush shape, the centre dot, the lock plane grid and Flatten's level.
		@param overlay
			The overlay renderer to queue shapes on.
		@param input
			The pointer and keys over the scene view this frame.
		@param context
			What the tool may reach in the editor.
		*/
	void BrushTool::drawOverlay(OverlayRenderer& overlay, const ViewportInput& input, ToolContext& context)
	{
		if (!cursor_.valid)
			return;
		using Layer = OverlayRenderer::Layer;
		const BrushSettings& s = settings_;
		const glm::vec3& c = cursor_.centre;
		const float r = s.radius(), h = s.halfHeight();

		glm::vec4 colour = removes(shownOp_)        ? kSubtractColour
		                   : shownOp_ == Op::Smooth ? kSmoothColour
		                                            : kAddColour;
		if (shownOp_ == Op::Paint || shownOp_ == Op::Replace)
		{
			const glm::vec3& m = context.voxel.materials().get(s.material).colourSrgb_;
			colour = { m.r, m.g, m.b, 0.30f };
		}

		// In the scene layer, so the ground cuts the shape where the brush is buried.
		switch (s.shape)
		{
			case voxel::edit::Shape::Sphere:
				overlay.sphere(Layer::Scene, c, r, colour);
				break;
			case voxel::edit::Shape::Cube:
				overlay.box(Layer::Scene, c - glm::vec3{ r, h, r }, c + glm::vec3{ r, h, r }, colour);
				break;
			case voxel::edit::Shape::Cylinder:
				overlay.cylinder(Layer::Scene, c - glm::vec3{ 0, h, 0 }, c + glm::vec3{ 0, h, 0 }, r, colour);
				break;
		}

		const float pixel = worldPerPixel(cursor_.point, input);
		overlay.sphere(Layer::OnTop, cursor_.point, 4.5f * pixel, kDotColour);

		// The plane the brush is held to, as a grid whose lines stay put in the world.
		if (const std::optional<BrushStroke::Plane> plane = stroke_.plane(s))
		{
			const float spacing = r < 2.f ? 0.5f : 1.f;
			const glm::vec3 offset = cursor_.point - plane->point;
			const glm::vec3 anchor =
				plane->point + glm::round(offset / spacing) * spacing
				- plane->normal * glm::dot(glm::round(offset / spacing) * spacing, plane->normal);
			overlay.grid(
				Layer::Scene,
				anchor + plane->normal * 0.01f,
				plane->normal,
				std::max(2.f, 2.5f * r),
				spacing,
				1.4f * pixel,
				input.eye,
				kGridColour);
		}

		// The level Flatten works to.
		if (desc_.flatten)
		{
			const float level = stroke_.active()                        ? flattenHeight_
			                    : s.flattenPlane == FlattenPlane::Fixed ? s.fixedPlaneHeight
			                                                            : cursor_.point.y;
			const glm::vec3 at{ c.x, level + 0.01f, c.z };
			overlay.disc(Layer::Scene, at, { 0.f, 1.f, 0.f }, r, kPlaneColour);
			overlay.circle(Layer::OnTop, at, { 0.f, 1.f, 0.f }, r, 1.6f * pixel, input.eye, kGridColour);
		}
	}
}
