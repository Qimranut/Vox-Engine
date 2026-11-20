/*************************************************************************************************************
\file       BrushTool.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The brush terrain tools (Draw, Sculpt, Smooth, Paint, Flatten, Erode): one class driven by
            a table of descriptors that say which op each resolves to and which settings it shows.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Editor
#include "BrushStroke.hpp"
#include "TerrainTool.hpp"
#include "ToolWidgets.hpp"

// stds
#include <cstdint>
#include <span>


namespace vox::editor
{
	/**
	 * What makes one brush tool different from another: which engine op it
	 * resolves to (through its BrushKind) and which of the shared settings it
	 * shows. The six brush tools are six rows of BrushTool::descriptors(); a
	 * seventh is one more row, and an op in the engine if it needs one.
	 *
	 */
	struct BrushToolDesc
	{
		BrushKind		kind;
		const char*		name;
		const char*		tip;
		widgets::Icon	icon;
		const char*		undoLabel;
		bool			mode;			// Add / Subtract
		bool			strength;
		bool			snapping;
		bool			planeLock;
		bool			flatten;		// flatten mode and flatten plane
		bool			material;		// what to add, with Auto Material
		bool			materialMode;	// Paint / Replace with a source material
	};

	/**
	 * A brush: follows the terrain under the pointer, stamps an op along a
	 * drag, and draws the translucent shape that shows where the next stamp
	 * lands. A whole press-drag-release is one transaction in the voxel
	 * system's history, so one Ctrl+Z takes the stroke back.
	 *
	 */
	class BrushTool final : public TerrainTool
	{
	public:

		/*!
			@brief
				Every brush tool there is, in the order the panel lists them.
			@return
				The descriptor table.
			*/
		static std::span<const BrushToolDesc> descriptors();

		/*!
			@brief
				Makes a brush tool from one row of the descriptor table.
			@param desc
				The tool's descriptor; must outlive the tool (the static table does).
			@param settings
				Shared by all brush tools and owned by the toolset, which outlives them.
			*/
		BrushTool(const BrushToolDesc& desc, BrushSettings& settings)
			:
			desc_{ desc },
			settings_{ settings }
		{
		}

		/*!
			@brief
				The tool's name, from its descriptor.
			@return
				The descriptor's name.
			*/
		const char*	name() const override	{ return desc_.name; }

		/*!
			@brief
				The tool's tooltip text, from its descriptor.
			@return
				The descriptor's tip.
			*/
		const char*	tip() const override	{ return desc_.tip; }

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
		void icon(ImDrawList& draw, const ImVec2& min, const ImVec2& max, unsigned int colour) const override;

		/*!
			@brief
				Draws the brush, material and filter settings this tool uses.
			@param context
				What the tool may reach in the editor.
			*/
		void drawSettings(ToolContext& context) override;

		/*!
			@brief
				Aims the brush, handles Alt+click picking and wheel sizing, and queues the stroke's stamps.
			@param input
				The pointer and keys over the scene view this frame.
			@param context
				What the tool may reach in the editor.
			*/
		void update(const ViewportInput& input, ToolContext& context) override;

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
		void drawOverlay(
			graphics::renderer::OverlayRenderer& overlay,
			const ViewportInput& input,
			ToolContext& context) override;

		/*!
			@brief
				Whether a stroke is in progress.
			@return
				True while the stroke is active.
			*/
		bool busy() const override { return stroke_.active(); }

		/*!
			@brief
				Cancels the stroke, clears the cursor and closes any open transaction.
			@param context
				What the tool may reach in the editor.
			*/
		void deactivate(ToolContext& context) override;

	private:

		/*!
			@brief
				Resizes the brush, its height (Ctrl) or strength (Shift) with the wheel while the adjust key is held.
			@param input
				The pointer and keys over the scene view this frame.
			*/
		void adjustWithWheel(const ViewportInput& input);

		/*!
			@brief
				Ends the open transaction, if any, and hands it to the editor's undo stack.
			@param context
				What the tool may reach in the editor.
			*/
		void finish(ToolContext& context);

		const BrushToolDesc&		desc_;
		BrushSettings&				settings_;
		BrushStroke					stroke_;
		BrushStroke::Cursor			cursor_;		// where the brush is this frame
		voxel::edit::Op				shownOp_		{ voxel::edit::Op::Add };	// what a click would do, with the keys held now
		voxel::edit::History::Id	transaction_	{};
		float						flattenHeight_	{};	// world y of the plane this stroke flattens to
		std::uint64_t				stamps_			{};	// counts stamps, so every Erode stamp gets its own seed
	};
}
