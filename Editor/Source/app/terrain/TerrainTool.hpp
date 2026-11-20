/*************************************************************************************************************
\file       TerrainTool.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The interface every terrain tool of the Terrain panel's Edit tab implements, with the
            viewport input and editor context handed to it each frame.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Editor
#include "../LogConsole.hpp"

// Engine
#include "util/geom/ray.hpp"
#include "voxel/edit/edit_history.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <functional>
#include <string>


struct ImDrawList;
struct ImVec2;

namespace voxel
{
	class System;
}

namespace graphics::renderer
{
	class OverlayRenderer;
}

namespace vox::editor
{
	/**
	 * The pointer and the keys over the scene view this frame, as a tool sees them.
	 *
	 */
	struct ViewportInput
	{
		// The camera, and the ray through the mouse position.
		util::geom::Ray	ray;
		glm::vec3		eye				{ 0.f };
		float			fovY			{ 1.f };
		float			viewportHeight	{ 720.f };

		// The pointer is over the view and nothing else owns it (no popup, no
		// look drag).
		bool			over			{};

		bool			leftDown		{};
		bool			leftPressed		{};
		bool			ctrl			{};
		bool			shift			{};
		bool			alt				{};

		bool			adjust			{};		// the brush-adjust key is held: the wheel resizes instead of zooming
		float			wheel			{};		// notches this frame

		// Pressed this frame, and no text box has the keyboard.
		bool			enter			{};
		bool			escape			{};

		double			time			{};		// seconds
	};

	/**
	 * What a tool may reach in the editor.
	 *
	 */
	struct ToolContext
	{
		voxel::System& voxel;

		std::function<void(LogLevel, std::string)> log;

		// A transaction the tool opened has been closed; the editor puts it on
		// its undo stack.
		std::function<void(voxel::edit::History::Id, std::string)> committed;
	};

	/**
	 * One entry of the Terrain panel's Edit tab. A tool owns its settings UI,
	 * reads the pointer, queues edits on the voxel system and draws its own
	 * cursor or gizmo; the panel and the viewport only ever talk to this
	 * interface, so a new tool is a new class and one line in TerrainToolset's
	 * constructor.
	 *
	 */
	class TerrainTool
	{
	public:

		virtual ~TerrainTool() = default;

		/*!
			@brief
				The tool's name, shown on its button and in the panel.
			@return
				A string literal.
			*/
		virtual const char* name() const = 0;

		/*!
			@brief
				The tool's tooltip text.
			@return
				A string literal.
			*/
		virtual const char* tip() const = 0;

		/*!
			@brief
				Draws the tool's icon into a square; ImGui draw-list primitives only.
			@param draw
				The draw list to draw into.
			@param min
				Top-left corner of the square.
			@param max
				Bottom-right corner of the square.
			@param colour
				Packed ImGui colour to draw with.
			*/
		virtual void icon(ImDrawList& draw, const ImVec2& min, const ImVec2& max, unsigned int colour) const = 0;

		/*!
			@brief
				Draws the tool's section of the panel. Between ImGui::Begin and End.
			@param context
				What the tool may reach in the editor.
			*/
		virtual void drawSettings(ToolContext& context) = 0;

		/*!
			@brief
				Runs once per frame while the tool is selected and the terrain can be edited.
			@param input
				The pointer and keys over the scene view this frame.
			@param context
				What the tool may reach in the editor.
			*/
		virtual void update(const ViewportInput& input, ToolContext& context) = 0;

		/*!
			@brief
				Queues the tool's cursor or gizmo; the viewport flushes the overlay afterwards.
			@param overlay
				The overlay renderer to queue shapes on.
			@param input
				The pointer and keys over the scene view this frame.
			@param context
				What the tool may reach in the editor.
			*/
		virtual void drawOverlay(graphics::renderer::OverlayRenderer& overlay,
								 const ViewportInput& input,
								 ToolContext& context) = 0;

		/*!
			@brief
				Whether the tool is in the middle of something undo must not interrupt.
			@return
				False unless a tool overrides it.
			*/
		virtual bool busy() const { return false; }

		/*!
			@brief
				Puts the tool down: another tool was chosen, Play began, the terrain was replaced.

				Finish or drop what is open.
			@param context
				What the tool may reach in the editor.
			*/
		virtual void deactivate(ToolContext& context) = 0;
	};
}
