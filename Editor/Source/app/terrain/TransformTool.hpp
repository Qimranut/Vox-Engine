/*************************************************************************************************************
\file       TransformTool.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The Transform terrain tool: selects a box of terrain, lifts it, and moves, turns and
            resizes it as one undoable transaction.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Editor
#include "TerrainTool.hpp"
#include "../gizmo/OrientedBoxGizmo.hpp"

// Voxel
#include "voxel/edit/region_transform.hpp"
#include "voxel/edit/voxel_region.hpp"

// stds
#include <memory>


namespace vox::editor
{
	/**
	 * Moves, turns and resizes a box of terrain.
	 *
	 * Two steps, because there is no separate selection tool: in Select the
	 * box is only a box, and its handles place it around the terrain to take;
	 * in Transform the terrain inside has been lifted, and the same handles -
	 * now with rotation rings - carry it. With Live Edit the terrain follows
	 * every change of the box; without, only the box moves until Apply. Either
	 * way the whole move is one transaction, so Cancel is a rollback and one
	 * Ctrl+Z undoes an applied move.
	 *
	 */
	class TransformTool final : public TerrainTool
	{
	public:

		/*!
			@brief
				The tool's name.
			@return
				"Transform".
			*/
		const char*	name() const override	{ return "Transform"; }

		/*!
			@brief
				The tool's tooltip text.
			@return
				A one-line description of the tool.
			*/
		const char*	tip() const override	{ return "Move, turn or resize a box of terrain."; }

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
		void icon(ImDrawList& draw, const ImVec2& min, const ImVec2& max, unsigned int colour) const override;

		/*!
			@brief
				Draws the Transform Settings section of the Terrain panel's Edit tab.
			@param context
				What the tool may reach in the editor.
			*/
		void drawSettings(ToolContext& context) override;

		/*!
			@brief
				Drives the box's gizmo with the pointer, carries out a lift, apply or cancel that was
				asked for, and with Live Edit keeps the terrain at the box.
			@param input
				The pointer and keys over the scene view this frame.
			@param context
				What the tool may reach in the editor.
			*/
		void update(const ViewportInput& input, ToolContext& context) override;

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
		void drawOverlay(graphics::renderer::OverlayRenderer& overlay,
						 const ViewportInput& input,
						 ToolContext& context) override;

		/*!
			@brief
				Whether terrain is lifted, so a transaction is open.
			@return
				True while lifted.
			*/
		bool busy() const override { return lifted_; }

		/*!
			@brief
				Ends a lift: a block that has been shown at the box is applied, one that never was is
				cancelled.
			@param context
				What the tool may reach in the editor.
			*/
		void deactivate(ToolContext& context) override;

		/** Largest box that can be lifted, per side, in voxels. */
		static constexpr int kMaxVoxels = 256;

	private:

		/*!
			@brief
				Puts the box on the ground in the middle of the terrain.
			@param context
				What the tool may reach in the editor.
			*/
		void placeInitialBox(ToolContext& context);

		/*!
			@brief
				Select -> Transform: copies the terrain in the box and opens the transaction.
			@param context
				What the tool may reach in the editor.
			*/
		void lift(ToolContext& context);

		/*!
			@brief
				Queues the block at the box's current placement, replacing what the transaction did before.
			@param context
				What the tool may reach in the editor.
			*/
		void place(ToolContext& context);

		/*!
			@brief
				Ends the transaction with the block where the box is; Transform -> Select.
			@param context
				What the tool may reach in the editor.
			*/
		void apply(ToolContext& context);

		/*!
			@brief
				Rolls the transaction back and returns the box to where it was lifted; Transform -> Select.
			@param context
				What the tool may reach in the editor.
			*/
		void cancel(ToolContext& context);

		/*!
			@brief
				Where the box puts the block.
			@param dims
				The world's dimensions, for the origin and voxel size.
			@return
				Centre in voxels, rotation, and scale against the lifted size.
			*/
		voxel::edit::Placement placement(const voxel::WorldDims& dims) const;

		/*!
			@brief
				The gizmo's size limits, snapping and rings for the current step.
			@param voxelSize
				Voxel edge length in metres.
			@return
				The gizmo options.
			*/
		OrientedBoxGizmo::Options options(float voxelSize) const;

		// The box and its handles.
		OrientedBoxGizmo	gizmo_;
		OrientedBox			box_;
		bool				placed_	{};		// the box has been put somewhere sensible for this terrain

		// The lift: what was taken, and what the open transaction shows of it.
		bool											lifted_			{};
		std::shared_ptr<const voxel::edit::VoxelRegion>	block_;
		OrientedBox										liftedFrom_;			// where the box was when it was lifted; Cancel goes back to it
		OrientedBox										queued_;				// the placement the terrain currently shows, or is about to
		bool											queuedMerge_	{};		// and the Merge Empty it was placed with
		bool											shown_			{};		// something has been queued since the lift
		voxel::edit::History::Id						transaction_	{};

		// The panel's checkboxes.
		bool	mergeEmpty_	{ false };
		bool	liveEdit_	{ true };
		bool	snap_		{ true };

		// What the panel's buttons asked for; carried out by the next update().
		bool	wantsApply_		{};
		bool	wantsCancel_	{};
		bool	wantsLift_		{};
	};
}
