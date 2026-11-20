/*************************************************************************************************************
\file       TransformTool.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The Transform terrain tool: box placement, lift, live placement, apply and cancel of a
            block of terrain, and its settings UI.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "TransformTool.hpp"

// Editor
#include "ToolWidgets.hpp"

// Voxel
#include "voxel/sys/voxel_sys.hpp"

// ImGui
#include <imgui.h>

// GLM
#include <glm/gtc/quaternion.hpp>

// stds
#include <algorithm>
#include <cmath>


namespace vox::editor
{
	namespace
	{
		// Colour for the "too many voxels" warning in drawSettings().
		const ImVec4 kWarningText(1.00f, 0.80f, 0.35f, 1);

		/*!
			@brief
				Translates the viewport input into the oriented box gizmo's input.
			@param input
				The pointer and keys over the scene view this frame.
			@return
				The gizmo input; Shift turns rings freely.
			*/
		OrientedBoxGizmo::Input gizmoInput(const ViewportInput& input)
		{
			OrientedBoxGizmo::Input in;
			in.ray = input.ray;
			in.overViewport = input.over;
			in.down = input.leftDown;
			in.pressed = input.leftPressed;
			in.freeAngle = input.shift;
			in.fovY = input.fovY;
			in.viewportHeight = input.viewportHeight;
			return in;
		}

		/*!
			@brief
				The world-aligned box that encloses a turned one.
			@param box
				The oriented box.
			@return
				An unrotated box with the same centre that contains it.
			*/
		OrientedBox enclosing(const OrientedBox& box)
		{
			const glm::mat3 rotate = glm::mat3_cast(box.rotation);
			glm::vec3 reach{ 0.f };
			for (int axis = 0; axis < 3; ++axis)
				for (int k = 0; k < 3; ++k)
					reach[axis] += std::fabs(rotate[k][axis]) * 0.5f * box.size[k];
			return OrientedBox{ box.centre, 2.f * reach, glm::quat{ 1.f, 0.f, 0.f, 0.f } };
		}
	}

	/*!
		@brief
			Draws the tool's button icon into the square from min to max.
		@param draw
			The draw list to draw into.
		@param min
			Top-left corner of the square.
		@param max
			Bottom-right corner of the square.
		@param colour
			Packed ImGui colour to draw with.
		*/
	void TransformTool::icon(ImDrawList& draw, const ImVec2& min, const ImVec2& max, unsigned int colour)
		const
	{
		widgets::drawIcon(draw, widgets::Icon::Transform, min, max, colour);
	}

	/*!
		@brief
			The gizmo's size limits, snapping and rings for the current step.

			A lifted block may be scaled up to 1.5 times the lift limit, but not without end.
		@param voxelSize
			Voxel edge length in metres.
		@return
			The gizmo options.
		*/
	OrientedBoxGizmo::Options TransformTool::options(float voxelSize) const
	{
		OrientedBoxGizmo::Options o;
		o.rings = lifted_;
		o.limits.minSize = glm::vec3{ 2.f * voxelSize };

		// A lifted block may be scaled up, but not without end: every voxel of the result is written.
		o.limits.maxSize = glm::vec3{ (lifted_ ? 1.5f : 1.f) * static_cast<float>(kMaxVoxels) * voxelSize };
		o.limits.snap = snap_ ? voxelSize : 0.f;
		return o;
	}

	/*!
		@brief
			Puts a box of up to 6 m on the ground in the middle of the terrain, half buried.
		@param context
			What the tool may reach in the editor.
		*/
	void TransformTool::placeInitialBox(ToolContext& context)
	{
		const voxel::WorldDims& dims = context.voxel.world()->dims();
		const glm::vec3 extent = glm::vec3(dims.voxels_) * dims.voxelSize_;
		const glm::vec3 middle = dims.origin_ + 0.5f * extent;

		// On the ground in the middle of the terrain, half buried so it holds something to move.
		float ground = middle.y;
		if (const auto hit = context.voxel.raycast(
				{ { middle.x, dims.origin_.y + extent.y + 1.f, middle.z }, { 0.f, -1.f, 0.f } }))
			ground = hit->point_.y;

		box_.size = glm::min(glm::vec3{ 6.f }, extent);
		box_.centre = { middle.x, ground, middle.z };
		box_.rotation = glm::quat{ 1.f, 0.f, 0.f, 0.f };
		placed_ = true;
	}

	/*!
		@brief
			Where the box puts the block.
		@param dims
			The world's dimensions, for the origin and voxel size.
		@return
			Centre in voxels, rotation, and scale against the lifted size.
		*/
	voxel::edit::Placement TransformTool::placement(const voxel::WorldDims& dims) const
	{
		voxel::edit::Placement p;
		p.centre_ = (box_.centre - dims.origin_) / dims.voxelSize_;
		p.rotation_ = box_.rotation;
		p.scale_ = box_.size / glm::max(liftedFrom_.size, glm::vec3{ 1e-4f });
		return p;
	}

	/*!
		@brief
			Select -> Transform: copies the terrain in the box and opens the transaction.

			The box is clipped to the world and to kMaxVoxels a side, then snapped to exactly the
			voxels taken. Nothing is lifted (and a warning logged) when the box holds no terrain.
		@param context
			What the tool may reach in the editor.
		*/
	void TransformTool::lift(ToolContext& context)
	{
		voxel::System& vox = context.voxel;
		const voxel::WorldDims& dims = vox.world()->dims();

		// The box as whole voxels, inside the world and no larger than can be carried.
		const glm::vec3 lo = (box_.centre - 0.5f * box_.size - dims.origin_) / dims.voxelSize_;
		const glm::vec3 hi = (box_.centre + 0.5f * box_.size - dims.origin_) / dims.voxelSize_;
		voxel::edit::VoxelBox voxels{ glm::ivec3(glm::round(lo)), glm::ivec3(glm::round(hi)) };
		voxels = voxels.clipped(voxel::edit::VoxelBox::ofWorld(dims));
		voxels.max_ = glm::min(voxels.max_, voxels.min_ + glm::ivec3{ kMaxVoxels });
		if (voxels.empty())
		{
			if (context.log)
				context.log(LogLevel::Warn, "The box is outside the terrain: there is nothing to lift.");
			return;
		}

		auto block = std::make_shared<voxel::edit::VoxelRegion>();
		vox.readRegion(voxels, *block);
		const bool anything = std::any_of(
			block->data(),
			block->data() + block->volume(),
			[](voxel::MaterialId m) { return m != voxel::kAir; });
		if (!anything)
		{
			if (context.log)
				context.log(LogLevel::Warn, "There is no terrain inside the box to lift.");
			return;
		}

		// The box becomes exactly the voxels that were taken, so an untouched block goes back
		// exactly where it came from.
		box_.rotation = glm::quat{ 1.f, 0.f, 0.f, 0.f };
		box_.size = glm::vec3(voxels.size()) * dims.voxelSize_;
		box_.centre = dims.origin_ + 0.5f * glm::vec3(voxels.min_ + voxels.max_) * dims.voxelSize_;

		block_ = std::move(block);
		liftedFrom_ = queued_ = box_;
		queuedMerge_ = mergeEmpty_;
		shown_ = false;
		transaction_ = vox.beginEdit("Transform terrain");
		lifted_ = transaction_ != 0;
		gizmo_.release();
	}

	/*!
		@brief
			Queues the block at the box's current placement, replacing what the transaction did before.
		@param context
			What the tool may reach in the editor.
		*/
	void TransformTool::place(ToolContext& context)
	{
		voxel::System& vox = context.voxel;

		// What the transaction has done so far goes first: the block is always placed onto
		// the terrain as it was when it was lifted, never onto its own last position.
		vox.rollbackEdit();
		vox.queueTransform(block_, placement(vox.world()->dims()), mergeEmpty_);
		queued_ = box_;
		queuedMerge_ = mergeEmpty_;
		shown_ = true;
	}

	/*!
		@brief
			Ends the transaction with the block where the box is; Transform -> Select.

			The box is then squared up around what was put down, ready to be lifted again.
		@param context
			What the tool may reach in the editor.
		*/
	void TransformTool::apply(ToolContext& context)
	{
		if (!lifted_)
			return;

		if (!shown_ || !(queued_ == box_) || queuedMerge_ != mergeEmpty_)
			place(context);
		context.voxel.endEdit();
		if (context.committed)
			context.committed(transaction_, "Transform terrain");

		// The box stays around what was just put down, squared up, ready to be lifted again.
		box_ = enclosing(box_);
		lifted_ = false;
		block_.reset();
		transaction_ = 0;
		gizmo_.release();
	}

	/*!
		@brief
			Rolls the transaction back and returns the box to where it was lifted; Transform -> Select.
		@param context
			What the tool may reach in the editor.
		*/
	void TransformTool::cancel(ToolContext& context)
	{
		if (!lifted_)
			return;

		context.voxel.rollbackEdit();
		context.voxel.endEdit(); // nothing left in it, so nothing is recorded
		box_ = liftedFrom_;
		lifted_ = false;
		block_.reset();
		transaction_ = 0;
		gizmo_.release();
	}

	/*!
		@brief
			Ends a lift: a block that has been shown at the box is applied, one that never was is
			cancelled.
		@param context
			What the tool may reach in the editor.
		*/
	void TransformTool::deactivate(ToolContext& context)
	{
		// What is on screen is what is kept: a block that shows where it was moved to stays
		// there, one that was never shown goes back.
		if (lifted_)
		{
			if (shown_)
				apply(context);
			else
				cancel(context);
		}
		gizmo_.release();
	}

	/*!
		@brief
			Drives the box's gizmo with the pointer, carries out a lift, apply or cancel that was
			asked for, and with Live Edit keeps the terrain at the box.

			Live Edit queues the next placement only once the last has landed.
		@param input
			The pointer and keys over the scene view this frame.
		@param context
			What the tool may reach in the editor.
		*/
	void TransformTool::update(const ViewportInput& input, ToolContext& context)
	{
		voxel::System& vox = context.voxel;
		const voxel::World* world = vox.world();
		if (!world || !vox.editable())
		{
			deactivate(context);
			placed_ = false;
			return;
		}

		if (!placed_)
			placeInitialBox(context);
		const float vs = world->dims().voxelSize_;

		gizmo_.update(box_, gizmoInput(input), options(vs));

		if (wantsLift_ || (input.enter && !lifted_))
			lift(context);
		else if (wantsApply_ || (input.enter && lifted_))
			apply(context);
		else if (wantsCancel_ || (input.escape && lifted_))
			cancel(context);
		wantsLift_ = wantsApply_ = wantsCancel_ = false;

		if (!lifted_)
			return;

		// Live Edit: the terrain follows the box, one placement at a time - the next is queued
		// only once the last has landed, so a fast drag skips positions instead of queueing them.
		const bool stale = !shown_ || !(queued_ == box_) || queuedMerge_ != mergeEmpty_;
		if (liveEdit_ && stale && !vox.editsPending())
			place(context);
		if (!liveEdit_ && shown_ && !vox.editsPending())
		{
			vox.rollbackEdit();
			shown_ = false;
		}
	}

	/*!
		@brief
			Draws the box and its handles, once the box has been placed.
		@param overlay
			The overlay renderer to queue shapes on.
		@param input
			The pointer and keys over the scene view this frame.
		@param context
			What the tool may reach in the editor.
		*/
	void TransformTool::drawOverlay(
		graphics::renderer::OverlayRenderer& overlay,
		const ViewportInput& input,
		ToolContext& context)
	{
		if (!placed_ || !context.voxel.world())
			return;

		gizmo_.draw(overlay, box_, gizmoInput(input), options(context.voxel.world()->dims().voxelSize_));
	}

	/*!
		@brief
			Draws the Transform Settings section: step, box size, position and rotation, options and
			the Lift / Apply / Cancel buttons.

			Button presses are recorded and carried out by the next update().
		@param context
			What the tool may reach in the editor.
		*/
	void TransformTool::drawSettings(ToolContext& context)
	{
		using namespace widgets;
		const voxel::World* world = context.voxel.world();
		const float vs = world ? world->dims().voxelSize_ : 0.1f;
		const OrientedBoxGizmo::Options limits = options(vs);

		if (!ImGui::CollapsingHeader("Transform Settings", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		// -------------------------------- Step --------------------------------

		int step = lifted_ ? 1 : 0;
		if (segmented("step", step, { "Select", "Transform" }))
		{
			if (step == 1)
				wantsLift_ = true;
			else
				wantsApply_ = true;
		}
		tooltip("Select: place the box around the terrain to take.\nTransform: the terrain in the box moves, "
		        "turns and resizes with it.");

		// --------------------------------- Box ---------------------------------

		ImGui::BeginDisabled(gizmo_.dragging());
		caption("Size");
		glm::vec3 size = box_.size;
		ImGui::SetNextItemWidth(-1);
		if (ImGui::DragFloat3("##size", &size.x, vs, 0.f, 0.f, "%.1f m"))
		{
			if (snap_)
				size = glm::round(size / vs) * vs;
			box_.size = glm::clamp(size, limits.limits.minSize, limits.limits.maxSize);
		}
		caption("Position");
		glm::vec3 position = box_.centre;
		ImGui::SetNextItemWidth(-1);
		if (ImGui::DragFloat3("##position", &position.x, vs, 0.f, 0.f, "%.1f m"))
			box_.centre = position;
		tooltip("The centre of the box.");

		caption("Rotation");
		ImGui::BeginDisabled(!lifted_);
		glm::vec3 degrees = glm::degrees(glm::eulerAngles(box_.rotation));
		ImGui::SetNextItemWidth(-1);
		if (ImGui::DragFloat3("##rotation", &degrees.x, 1.f, -180.f, 180.f, "%.0f deg"))
			box_.rotation = glm::normalize(glm::quat(glm::radians(degrees)));
		ImGui::EndDisabled();
		tooltip(
			lifted_ ? "Or drag a ring in the viewport. Rings snap to 15 degrees; hold Shift to turn freely."
					: "A box can be turned once its terrain is lifted (Transform).");
		ImGui::EndDisabled();

		// ------------------------------- Options -------------------------------

		ImGui::Spacing();
		ImGui::Checkbox("Merge Empty", &mergeEmpty_);
		tooltip("On: the empty parts of the block leave the terrain they land on alone.\nOff: the block "
		        "arrives with its own air and clears what was there.");
		ImGui::Checkbox("Live Edit", &liveEdit_);
		tooltip("On: the terrain follows the box as it moves.\nOff: only the box moves until Apply.");
		ImGui::Checkbox("Snap to Voxels", &snap_);

		// ------------------------------- Actions -------------------------------

		ImGui::Spacing();
		const glm::ivec3 voxels(glm::round(box_.size / vs));
		ImGui::TextDisabled("%d x %d x %d voxels", voxels.x, voxels.y, voxels.z);
		if (!lifted_ && glm::any(glm::greaterThan(voxels, glm::ivec3{ kMaxVoxels })))
			ImGui::TextColored(
				kWarningText,
				"More than %d voxels on a side: only that much is lifted.",
				kMaxVoxels);

		if (!lifted_)
		{
			if (ImGui::Button("Lift terrain", ImVec2(-1, 28)))
				wantsLift_ = true;
			tooltip("Takes the terrain inside the box (Enter).");
		}
		else
		{
			const float half = 0.5f * (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.95f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.52f, 1.00f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.36f, 0.85f, 1));
			if (ImGui::Button("Apply", ImVec2(half, 28)))
				wantsApply_ = true;
			ImGui::PopStyleColor(3);
			tooltip("Keeps the terrain where the box is (Enter).");
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(half, 28)))
				wantsCancel_ = true;
			tooltip("Puts the terrain back where it was lifted from (Esc).");
		}
	}
}
