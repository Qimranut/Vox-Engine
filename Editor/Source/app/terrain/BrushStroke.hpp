/*************************************************************************************************************
\file       BrushStroke.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Where a terrain brush goes and when it stamps (pivot, snapping, plane lock, stamp spacing,
            repeat rate), and resolveOp, which turns a tool, its settings and modifier keys into an
            engine edit op.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Editor
#include "BrushSettings.hpp"

// Engine
#include "util/geom/ray.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <optional>
#include <string>
#include <vector>


namespace vox::editor
{
	/**
	 * Which brush tool is driving a stroke. The engine knows ops, not tools:
	 * resolveOp() is where a tool, its settings and the held modifier keys
	 * become one.
	 *
	 */
	enum class BrushKind
	{
		Draw,
		Sculpt,
		Smooth,
		Paint,
		Flatten,
		Erode
	};

	/**
	 * Where the mouse ray met the terrain, as the voxel raycast reports it.
	 *
	 */
	struct SurfaceHit
	{
		glm::vec3			point		{ 0.f };
		glm::vec3			normal		{ 0.f, 1.f, 0.f };
		voxel::MaterialId	material	{ voxel::kAir };
	};

	/**
	 * Where a brush goes and when it stamps: pivot, snapping, plane lock, the
	 * spacing of stamps along a drag and the repeat rate of a brush held still.
	 * Pure logic over a ray and a hit, with no ImGui, GL or voxel system in it,
	 * because none of it can be clicked in an automated run: selfCheck() is
	 * what guards it.
	 *
	 */
	class BrushStroke
	{
	public:

		struct Input
		{
			util::geom::Ray				ray;
			std::optional<SurfaceHit>	hit;		// empty when the ray misses the terrain
			bool						over	{};	// the pointer is over the viewport and the viewport is free
			bool						down	{};	// left button held
			bool						pressed	{};	// went down this frame
			double						time	{};	// seconds, monotonic
		};

		struct Plane
		{
			glm::vec3	point	{ 0.f };
			glm::vec3	normal	{ 0.f, 1.f, 0.f };
		};

		struct Cursor
		{
			bool				valid		{};
			glm::vec3			point		{ 0.f };	// on the terrain or on the locked plane
			glm::vec3			centre		{ 0.f };	// of the brush, after pivot and snapping
			voxel::MaterialId	material	{ voxel::kAir };
		};

		struct Frame
		{
			Cursor						cursor;
			std::vector<glm::vec3>	stamps;	// brush centres to apply this frame, in order
			bool					began	{};
			bool					ended	{};
		};

		/*!
			@brief
				Advances the stroke by one frame: places the cursor, begins or ends the stroke and
				lists the stamps to apply.
			@param settings
				The brush settings in use.
			@param input
				This frame's mouse ray, terrain hit and button state.
			@param voxelSize
				Voxel edge length in metres, for snapping and the minimum stamp spacing.
			@param gridOrigin
				Origin of the voxel lattice, for snapping.
			@param repeats
				Whether the tool keeps stamping while held still (the gradual ones do).
			@return
				The cursor, the stamps for this frame and whether the stroke began or ended.
			*/
		Frame update(
			const BrushSettings& settings,
			const Input& input,
			float voxelSize,
			const glm::vec3& gridOrigin,
			bool repeats);

		/*!
			@brief
				Whether a stroke is in progress.
			@return
				True between the press that began a stroke and its end.
			*/
		bool active() const { return active_; }

		/*!
			@brief
				Ends the stroke without a final frame: the tool was switched away or the level closed.
			*/
		void cancel()
		{
			active_ = false;
			locked_.reset();
		}

		/*!
			@brief
				The plane the brush is held to right now, for drawing its grid.
			@param settings
				The brush settings, whose plane lock decides the plane.
			@return
				The Manual plane, the Auto plane of an active stroke, or empty when none holds the brush.
			*/
		std::optional<Plane> plane(const BrushSettings& settings) const;

		/*!
			@brief
				Where the stroke began; the Flatten tool's Auto plane is this height.
			@return
				The terrain or plane point under the pointer when the stroke began.
			*/
		const glm::vec3& startPoint() const { return start_; }

		/*!
			@brief
				Runs the stroke and resolveOp checks without a window.
			@return
				Empty when every check passes, else what failed.
			*/
		static std::string selfCheck();

	private:

		bool					active_		{};
		glm::vec3				start_		{ 0.f };
		glm::vec3				last_		{ 0.f };
		double					lastTime_	{};
		std::optional<Plane>	locked_;	// Auto plane lock, held for the length of a stroke
	};

	/*!
		@brief
			The engine op a tool performs, given the settings and the modifier keys.

			Ctrl swaps Add and Subtract, Shift turns Draw and Sculpt into Smooth for as long as it
			is held. Leaves the flatten plane and the erosion seed to the caller, which knows the
			stroke.
		@param kind
			The brush tool driving the stroke.
		@param settings
			The shared brush settings.
		@param ctrl
			Whether Ctrl is held.
		@param shift
			Whether Shift is held.
		@param voxelSize
			Voxel edge length in metres, for turning the height filter's metres into voxel rows.
		@param gridOriginY
			World y of the voxel lattice's origin, for the same conversion.
		@return
			The op, its strength, material and mask.
		*/
	voxel::edit::OpParams resolveOp(
		BrushKind kind,
		const BrushSettings& settings,
		bool ctrl,
		bool shift,
		float voxelSize,
		float gridOriginY);

	/*!
		@brief
			Whether a tool keeps applying while the brush is held still.
		@param op
			The engine op the tool performs.
		@return
			True for gradual ops.
		*/
	bool repeatsWhileHeld(voxel::edit::Op op);
}
