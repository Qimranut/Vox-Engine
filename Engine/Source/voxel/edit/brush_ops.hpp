/*************************************************************************************************************
\file       brush_ops.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Brush operations: the Op enum, masks and parameters, the op table, and apply(), which runs
            one stamp of an op over a VoxelRegion as a pure function.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "brush.hpp"
#include "voxel_region.hpp"

// stds
#include <cstdint>
#include <span>
#include <string_view>


namespace voxel::edit
{
	/**
	 * What a brush does to the voxels it covers. One row of the table in
	 * brush_ops.cpp each: a new op is a new row and a function, and nothing
	 * else in the engine changes. The Editor's tools name ops; they never
	 * touch voxels themselves.
	 *
	 */
	enum class Op : std::uint8_t
	{
		Add,		// fill empty voxels
		Subtract,	// empty every voxel
		Grow,		// push the surface outwards, gradually
		Shrink,		// pull it inwards, gradually
		Smooth,		// remove spikes, fill pits
		Flatten,	// bring the surface to a horizontal plane
		Paint,		// change the material of solid voxels
		Replace,	// change one material into another
		Erode,		// run water and gravity over the surface
		Fill,		// set every voxel, solid or empty, to one material
		Count
	};

	enum class FlattenMode : std::uint8_t
	{
		Both,		// lower what is above the plane and raise what is below
		Lower,		// only remove what stands above the plane
		Raise		// only fill what lies below the plane
	};

	/**
	 * Which voxels a brush is allowed to change, beyond its shape. This is
	 * what makes a brush selective: paint only steep ground, only above the
	 * snow line, only what is currently grass.
	 *
	 */
	struct Mask
	{
		bool		useMaterial_	{ false };
		MaterialId	material_		{ kAir };		// only voxels of this material

		bool		useSlope_		{ false };
		float		slopeMinDeg_	{ 0.f };		// 0 is flat ground, 90 a wall
		float		slopeMaxDeg_	{ 90.f };

		bool		useHeight_		{ false };
		int			minY_			{ 0 };			// voxel rows, inclusive
		int			maxY_			{ 0 };

		/*!
			@brief
				Whether any mask condition is enabled.
			@return
				True when material, slope or height masking is on
			*/
		bool any() const { return useMaterial_ || useSlope_ || useHeight_; }
	};

	struct OpParams
	{
		Op			op_				{ Op::Add };
		float		strength_		{ 1.f };			// 0..1; gradual ops only
		MaterialId	material_		{ 1 };				// what Add, Grow, Flatten, Paint and Replace write
		MaterialId	source_			{ kAir };			// what Replace looks for

		// New voxels take the material of the ground they extend instead of material_.
		bool		autoMaterial_	{ false };

		FlattenMode	flattenMode_	{ FlattenMode::Both };
		float		planeY_			{ 0.f };			// Flatten: height of the plane, in voxel units

		Mask		mask_;

		// Erode only: every stamp of a stroke should pass a different value, or
		// each one would send its droplets down the same paths.
		std::uint64_t seed_			{ 0 };
	};

	struct OpInfo
	{
		Op					id_;
		std::string_view	name_;
		bool				gradual_;		// strength and falloff apply
		bool				reshapes_;		// can change which voxels are solid, not only their material
	};

	/*!
		@brief
			Every op, in enum order.
		@return
			The op table
		*/
	std::span<const OpInfo> ops();

	/*!
		@brief
			The table row of one op.
		@param op
			Op to look up
		@return
			Its OpInfo
		*/
	const OpInfo& info(Op op);

	/*!
		@brief
			The voxels an op needs to see to do one stamp.

			The brush's bounds plus the surroundings its filter reads. Read this
			box, apply(), write it back.
		@param stamp
			The brush application
		@param params
			The op and its settings
		@return
			The box to read
		*/
	VoxelBox contextBox(const Stamp& stamp, const OpParams& params);

	/*!
		@brief
			Applies one stamp to a region in place.

			Only voxels the brush covers change; the rest of the region is
			context. A region smaller than contextBox() works, reading air beyond
			its edges. A pure function of its arguments, so the same stroke always
			produces the same terrain.
		@param region
			Voxels to edit, in place
		@param stamp
			The brush application
		@param params
			The op and its settings
		@return
			Voxels changed
		*/
	std::size_t apply(VoxelRegion& region, const Stamp& stamp, const OpParams& params);
}
