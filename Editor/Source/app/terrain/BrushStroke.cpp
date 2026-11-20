/*************************************************************************************************************
\file       BrushStroke.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Brush stroke logic: cursor placement, stroke begin/end, stamp spacing and repeat, the
            tool-to-op mapping, and the self-check that guards them.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "BrushStroke.hpp"

// stds
#include <algorithm>
#include <cmath>


namespace vox::editor
{
	namespace
	{
		// Stamps along a drag sit this share of the radius apart: close enough that a sphere dragged
		// across the ground leaves a trench and not a row of pits.
		constexpr float kSpacing = 0.25f;

		// A gradual brush held still keeps working at this rate.
		constexpr double kRepeatSeconds = 1.0 / 15.0;

		// A drag that jumps across the view is filled in, but only so far in one frame.
		constexpr int kMaxStampsPerFrame = 64;

		// Beyond this the ray met the lock plane at a grazing angle and the point is meaningless.
		constexpr float kMaxPlaneDistance = 2000.f;

		/*!
			@brief
				Rounds a point to the nearest voxel lattice point.
			@param p
				The point in world space.
			@param voxelSize
				Voxel edge length in metres.
			@param origin
				Origin of the lattice.
			@return
				The nearest lattice point.
			*/
		glm::vec3 snapped(const glm::vec3& p, float voxelSize, const glm::vec3& origin)
		{
			return origin + glm::round((p - origin) / voxelSize) * voxelSize;
		}
	}

	/*!
		@brief
			The plane the brush is held to right now, for drawing its grid.
		@param settings
			The brush settings, whose plane lock decides the plane.
		@return
			The Manual plane, the Auto plane of an active stroke, or empty when none holds the brush.
		*/
	std::optional<BrushStroke::Plane> BrushStroke::plane(const BrushSettings& settings) const
	{
		if (settings.planeLock == PlaneLock::Manual)
			return Plane{ { 0.f, settings.manualPlaneHeight, 0.f }, { 0.f, 1.f, 0.f } };
		if (settings.planeLock == PlaneLock::Auto && active_)
			return locked_;
		return std::nullopt;
	}

	/*!
		@brief
			Advances the stroke by one frame: places the cursor, begins or ends the stroke and lists
			the stamps to apply.
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
	BrushStroke::Frame BrushStroke::update(
		const BrushSettings& settings,
		const Input& input,
		float voxelSize,
		const glm::vec3& gridOrigin,
		bool repeats)
	{
		Frame frame;
		const std::optional<Plane> held = plane(settings);

		// ------------------------------- Cursor --------------------------------

		// On the plane while one is held, otherwise on the terrain.
		Cursor& cursor = frame.cursor;
		if (held)
		{
			const std::optional<float> t = util::geom::rayPlane(input.ray, held->point, held->normal);
			if (t && *t < kMaxPlaneDistance)
			{
				cursor.valid = true;
				cursor.point = input.ray.at(*t);
			}
		}
		else if (input.hit)
		{
			cursor.valid = true;
			cursor.point = input.hit->point;
		}
		if (input.hit)
			cursor.material = input.hit->material;
		cursor.valid = cursor.valid && (input.over || active_);

		// -------------------------------- Pivot ---------------------------------

		// The pivot says which part of the brush sits on that point.
		const glm::vec3 up = held ? held->normal : glm::vec3{ 0.f, 1.f, 0.f };
		const float lift = settings.pivot == BrushPivot::Bottom ? settings.halfHeight()
		                   : settings.pivot == BrushPivot::Top  ? -settings.halfHeight()
		                                                        : 0.f;
		const auto place = [&](const glm::vec3& point)
		{
			const glm::vec3 centre = point + up * lift;
			return settings.snapping == BrushSnapping::Voxel ? snapped(centre, voxelSize, gridOrigin)
			                                                 : centre;
		};
		cursor.centre = place(cursor.point);

		// -------------------------------- Begin ---------------------------------

		if (!active_)
		{
			if (input.pressed && input.over && cursor.valid)
			{
				active_ = true;
				frame.began = true;
				start_ = cursor.point;
				last_ = cursor.centre;
				lastTime_ = input.time;
				frame.stamps.push_back(cursor.centre);
				// Auto holds the brush to the face the stroke began on: drawing over ground
				// stays level instead of climbing what it has just added.
				if (settings.planeLock == PlaneLock::Auto)
				{
					glm::vec3 normal = input.hit ? input.hit->normal : glm::vec3{ 0.f };
					if (glm::length(normal) < 0.5f)
						normal = { 0.f, 1.f, 0.f };
					locked_ = Plane{ cursor.point, glm::normalize(normal) };
				}
			}
			return frame;
		}

		// --------------------------------- End ----------------------------------

		if (!input.down)
		{
			active_ = false;
			locked_.reset();
			frame.ended = true;
			return frame;
		}
		if (!cursor.valid)
			return frame;

		// -------------------------------- Stamps --------------------------------

		const float spacing = std::max(voxelSize, kSpacing * settings.radius());
		const glm::vec3 travel = cursor.centre - last_;
		const float distance = glm::length(travel);
		if (distance >= spacing)
		{
			const int count = std::min(static_cast<int>(distance / spacing), kMaxStampsPerFrame);
			const glm::vec3 step = travel / distance * spacing;
			for (int i = 1; i <= count; ++i)
			{
				const glm::vec3 centre = last_ + step * static_cast<float>(i);
				frame.stamps.push_back(
					settings.snapping == BrushSnapping::Voxel ? snapped(centre, voxelSize, gridOrigin)
															  : centre);
			}
			last_ += step * static_cast<float>(count);
			lastTime_ = input.time;
		}
		else if (repeats && input.time - lastTime_ >= kRepeatSeconds)
		{
			frame.stamps.push_back(cursor.centre);
			lastTime_ = input.time;
		}
		return frame;
	}

	/*!
		@brief
			Whether a tool keeps applying while the brush is held still.
		@param op
			The engine op the tool performs.
		@return
			True for gradual ops.
		*/
	bool repeatsWhileHeld(voxel::edit::Op op)
	{
		return voxel::edit::info(op).gradual_;
	}

	/*!
		@brief
			The engine op a tool performs, given the settings and the modifier keys.

			Ctrl swaps Add and Subtract, Shift turns Draw and Sculpt into Smooth. The flatten plane
			and the erosion seed are left to the caller.
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
		float gridOriginY)
	{
		using voxel::edit::Op;
		voxel::edit::OpParams params;
		params.strength_ = settings.strength;
		params.material_ = settings.material;
		params.autoMaterial_ = settings.autoMaterial;
		params.flattenMode_ = settings.flattenMode;

		// Ctrl is "the other one" for as long as it is held, whichever mode is selected.
		const bool subtract = (settings.mode == BrushMode::Subtract) != ctrl;
		switch (kind)
		{
			case BrushKind::Draw:
				params.op_ = shift ? Op::Smooth : subtract ? Op::Subtract : Op::Add;
				break;
			case BrushKind::Sculpt:
				params.op_ = shift ? Op::Smooth : subtract ? Op::Shrink : Op::Grow;
				break;
			case BrushKind::Smooth:
				params.op_ = Op::Smooth;
				break;
			case BrushKind::Flatten:
				params.op_ = Op::Flatten;
				break;
			case BrushKind::Erode:
				params.op_ = Op::Erode;
				break;
			case BrushKind::Paint:
				params.op_ = settings.materialMode == MaterialMode::Replace ? Op::Replace : Op::Paint;
				params.source_ = settings.source;
				break;
		}

		voxel::edit::Mask& mask = params.mask_;
		mask.useSlope_ = settings.filterSlope;
		mask.slopeMinDeg_ = settings.slopeMin;
		mask.slopeMaxDeg_ = settings.slopeMax;
		mask.useHeight_ = settings.filterHeight;
		// The nudge keeps a height typed as a whole number of voxels from rounding down a row.
		mask.minY_ = static_cast<int>(std::floor((settings.heightMin - gridOriginY) / voxelSize + 1e-3f));
		mask.maxY_ = static_cast<int>(std::floor((settings.heightMax - gridOriginY) / voxelSize + 1e-3f));
		mask.useMaterial_ = settings.filterMaterial;
		mask.material_ = settings.filterMaterialId;
		return params;
	}

	/*!
		@brief
			Runs the stroke and resolveOp checks without a window.
		@return
			Empty when every check passes, else what failed.
		*/
	std::string BrushStroke::selfCheck()
	{
		using voxel::edit::Op;
		const auto nearly = [](const glm::vec3& a, const glm::vec3& b)
		{
			return glm::length(a - b) < 1e-3f;
		};
		const glm::vec3 origin{ 0.f };
		constexpr float vs = 0.1f;

		// A pointer looking straight down at flat ground of height 2, at (x, z).
		const auto lookingAt = [](float x, float z, bool down, bool pressed, double time, float ground = 2.f)
		{
			Input in;
			in.ray = { { x, 50.f, z }, { 0.f, -1.f, 0.f } };
			in.hit = SurfaceHit{ { x, ground, z }, { 0.f, 1.f, 0.f }, 3 };
			in.over = true;
			in.down = down;
			in.pressed = pressed;
			in.time = time;
			return in;
		};

		BrushSettings settings;
		settings.size = 4.f; // radius 2, so stamps are 0.5 apart

		{ // The pivot says which part of the brush touches the ground.
			BrushStroke stroke;
			settings.pivot = BrushPivot::Centre;
			if (!nearly(
					stroke.update(settings, lookingAt(1, 1, false, false, 0), vs, origin, false)
						.cursor.centre,
					{ 1, 2, 1 }))
				return "a centre pivot puts the brush's middle on the ground";
			settings.pivot = BrushPivot::Bottom;
			if (!nearly(
					stroke.update(settings, lookingAt(1, 1, false, false, 0), vs, origin, false)
						.cursor.centre,
					{ 1, 4, 1 }))
				return "a bottom pivot rests the brush on the ground";
			settings.pivot = BrushPivot::Top;
			if (!nearly(
					stroke.update(settings, lookingAt(1, 1, false, false, 0), vs, origin, false)
						.cursor.centre,
					{ 1, 0, 1 }))
				return "a top pivot hangs the brush below the ground";
			settings.shape = voxel::edit::Shape::Cylinder;
			settings.height = 1.f;
			settings.pivot = BrushPivot::Bottom;
			if (!nearly(
					stroke.update(settings, lookingAt(1, 1, false, false, 0), vs, origin, false)
						.cursor.centre,
					{ 1, 2.5f, 1 }))
				return "a cylinder's pivot uses its height, not its radius";
			settings.shape = voxel::edit::Shape::Sphere;
			settings.pivot = BrushPivot::Centre;
		}
		{ // Snapping lands the centre on the voxel lattice.
			BrushStroke stroke;
			settings.snapping = BrushSnapping::Voxel;
			const glm::vec3 c =
				stroke.update(settings, lookingAt(1.234f, 5.678f, false, false, 0), vs, origin, false)
					.cursor.centre;
			if (!nearly(c, { 1.2f, 2.f, 5.7f }))
				return "voxel snapping rounds the brush centre to the lattice";
			settings.snapping = BrushSnapping::Off;
		}
		{ // A drag stamps at even spacing, and a press stamps at once.
			BrushStroke stroke;
			Frame f = stroke.update(settings, lookingAt(0, 0, true, true, 0), vs, origin, false);
			if (!f.began || f.stamps.size() != 1 || !stroke.active())
				return "a press starts a stroke with one stamp";
			f = stroke.update(settings, lookingAt(0.3f, 0, true, false, 0.01), vs, origin, false);
			if (!f.stamps.empty())
				return "a move shorter than the spacing stamps nothing";
			f = stroke.update(settings, lookingAt(2.6f, 0, true, false, 0.02), vs, origin, false);
			if (f.stamps.size() != 5 || !nearly(f.stamps.back(), { 2.5f, 2, 0 }))
				return "a long move is filled with evenly spaced stamps";
			f = stroke.update(settings, lookingAt(2.6f, 0, true, false, 5.0), vs, origin, false);
			if (!f.stamps.empty())
				return "a brush that does not repeat stamps nothing while held still";
			f = stroke.update(settings, lookingAt(2.6f, 0, true, false, 5.1), vs, origin, true);
			if (f.stamps.size() != 1)
				return "a brush that repeats keeps stamping while held still";
			f = stroke.update(settings, lookingAt(2.6f, 0, false, false, 5.2), vs, origin, true);
			if (!f.ended || stroke.active())
				return "releasing the button ends the stroke";
		}
		{ // A press outside the viewport, or on nothing, starts no stroke.
			BrushStroke stroke;
			Input in = lookingAt(0, 0, true, true, 0);
			in.over = false;
			if (stroke.update(settings, in, vs, origin, false).began)
				return "a press outside the viewport starts no stroke";
			in.over = true;
			in.hit.reset();
			if (stroke.update(settings, in, vs, origin, false).began)
				return "a press on empty sky starts no stroke";
		}
		{ // Auto plane lock: the stroke stays at the height it began at, whatever it passes over.
			BrushStroke stroke;
			settings.planeLock = PlaneLock::Auto;
			stroke.update(settings, lookingAt(0, 0, true, true, 0), vs, origin, false);
			const Frame f =
				stroke.update(settings, lookingAt(3, 0, true, false, 0.1, 7.f), vs, origin, false);
			if (f.stamps.empty() || std::fabs(f.stamps.back().y - 2.f) > 1e-3f)
				return "an auto plane lock keeps the stroke on the plane it began on";
			if (!stroke.plane(settings))
				return "a locked stroke reports its plane";
			stroke.update(settings, lookingAt(3, 0, false, false, 0.2), vs, origin, false);
			if (stroke.plane(settings))
				return "the auto plane is released with the stroke";
			settings.planeLock = PlaneLock::Manual;
			settings.manualPlaneHeight = 9.f;
			if (std::fabs(
					stroke.update(settings, lookingAt(1, 1, false, false, 0.3), vs, origin, false)
						.cursor.centre.y
					- 9.f)
			    > 1e-3f)
				return "a manual plane holds the brush at its height";
			settings.planeLock = PlaneLock::Off;
		}
		{ // Tool + settings + modifier keys -> engine op.
			if (resolveOp(BrushKind::Draw, settings, false, false, vs, 0).op_ != Op::Add)
				return "Draw adds";
			if (resolveOp(BrushKind::Draw, settings, true, false, vs, 0).op_ != Op::Subtract)
				return "Ctrl makes Draw subtract";
			if (resolveOp(BrushKind::Draw, settings, false, true, vs, 0).op_ != Op::Smooth)
				return "Shift makes Draw smooth";
			if (resolveOp(BrushKind::Sculpt, settings, false, false, vs, 0).op_ != Op::Grow)
				return "Sculpt grows";
			settings.mode = BrushMode::Subtract;
			if (resolveOp(BrushKind::Sculpt, settings, false, false, vs, 0).op_ != Op::Shrink)
				return "Sculpt in Subtract mode shrinks";
			if (resolveOp(BrushKind::Sculpt, settings, true, false, vs, 0).op_ != Op::Grow)
				return "Ctrl swaps the mode back";
			settings.mode = BrushMode::Add;
			if (resolveOp(BrushKind::Smooth, settings, true, true, vs, 0).op_ != Op::Smooth)
				return "Smooth ignores the modifiers";
			if (resolveOp(BrushKind::Paint, settings, false, false, vs, 0).op_ != Op::Paint)
				return "Paint paints";
			settings.materialMode = MaterialMode::Replace;
			settings.source = 2;
			const voxel::edit::OpParams replace = resolveOp(BrushKind::Paint, settings, false, false, vs, 0);
			if (replace.op_ != Op::Replace || replace.source_ != 2)
				return "Paint in Replace mode replaces its source material";
			settings.materialMode = MaterialMode::Paint;
			settings.filterHeight = true;
			settings.heightMin = 1.f;
			settings.heightMax = 2.05f;
			const voxel::edit::OpParams banded =
				resolveOp(BrushKind::Paint, settings, false, false, vs, -1.f);
			if (banded.mask_.minY_ != 20 || banded.mask_.maxY_ != 30)
				return "the height filter becomes voxel rows above the grid's origin";
			if (!repeatsWhileHeld(Op::Grow) || repeatsWhileHeld(Op::Add))
				return "gradual ops repeat while held; Draw does not";
		}
		return {};
	}
}
