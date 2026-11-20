/*************************************************************************************************************
\file       BrushSettings.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The settings every terrain brush tool shares (shape, size, pivot, plane lock, flatten,
            material and filters) and the enums that describe them.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Engine
#include "voxel/edit/brush_ops.hpp"

// GLM
#include <glm/glm.hpp>


namespace vox::editor
{
	/** Whether a brush puts terrain down or takes it away. */
	enum class BrushMode
	{
		Add,
		Subtract
	};

	/** Which part of the brush sits on the point under the pointer. */
	enum class BrushPivot
	{
		Bottom,
		Centre,
		Top
	};

	/** Whether the brush centre is rounded to the voxel lattice. */
	enum class BrushSnapping
	{
		Off,
		Voxel
	};

	/**
	 * What holds the brush to a plane. Off: the brush follows the terrain.
	 * Auto: a stroke stays on the face it began on. Manual: the brush stays on
	 * a level plane the user places.
	 *
	 */
	enum class PlaneLock
	{
		Off,
		Auto,
		Manual
	};

	/**
	 * The height Flatten works to. Auto: the height where the stroke begins.
	 * Fixed: a height the user types.
	 *
	 */
	enum class FlattenPlane
	{
		Auto,
		Fixed
	};

	/** Whether Paint changes every material it covers or replaces one source material. */
	enum class MaterialMode
	{
		Paint,
		Replace
	};

	/**
	 * Every brush tool's settings. One set shared by all of them, so a size or
	 * material chosen for Draw is still there after a detour through Smooth;
	 * each tool shows the subset it uses. Lengths are in metres: the tools turn
	 * them into voxels when they build a stamp.
	 *
	 */
	struct BrushSettings
	{
		// Shape, size and where the brush sits.
		BrushMode			mode				{ BrushMode::Add };
		voxel::edit::Shape	shape				{ voxel::edit::Shape::Sphere };
		float				size				{ 3.f };	// diameter, or the side of a cube
		float				height				{ 3.f };	// cube and cylinder only
		bool				linkHeight			{ true };	// height follows size until the user sets it apart
		float				strength			{ 0.6f };
		BrushPivot			pivot				{ BrushPivot::Centre };
		BrushSnapping		snapping			{ BrushSnapping::Off };
		PlaneLock			planeLock			{ PlaneLock::Off };
		float				manualPlaneHeight	{ 5.f };	// world y of the Manual plane
		bool				ignoreWater			{ false };

		// Flatten.
		voxel::edit::FlattenMode	flattenMode			{ voxel::edit::FlattenMode::Both };
		FlattenPlane				flattenPlane		{ FlattenPlane::Auto };
		float						fixedPlaneHeight	{ 5.f };	// world y of the Fixed flatten plane

		// Material.
		MaterialMode		materialMode	{ MaterialMode::Paint };
		bool				autoMaterial	{ false };
		voxel::MaterialId	material		{ 3 };	// what is added or painted
		voxel::MaterialId	source			{ 1 };	// Replace: what gets replaced

		// Filters: which ground a brush may touch at all.
		bool				filterSlope			{ false };
		float				slopeMin			{ 0.f };
		float				slopeMax			{ 90.f };
		bool				filterHeight		{ false };
		float				heightMin			{ 0.f };	// world y
		float				heightMax			{ 20.f };	// world y
		bool				filterMaterial		{ false };
		voxel::MaterialId	filterMaterialId	{ 1 };

		// The range the size and height sliders and the wheel keep to, in metres.
		static constexpr float kMinSize = 0.2f;
		static constexpr float kMaxSize = 12.8f;

		/*!
			@brief
				Radius of the brush in metres (half its size).
			@return
				Half of size.
			*/
		float	radius() const	{ return 0.5f * size; }

		/*!
			@brief
				Vertical half extent of the brush as it is shaped now.
			@return
				The radius for a sphere, half the height for a cube or cylinder, in metres.
			*/
		float halfHeight() const { return shape == voxel::edit::Shape::Sphere ? radius() : 0.5f * height; }
	};
}
