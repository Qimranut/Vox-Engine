/*************************************************************************************************************
\file       voxel_raycast.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Grid raycast against level 0 of the voxel world, its options and hit result; what editor
            brushes aim with, since it sees edits the frame they land.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"
#include "../world/world.hpp"

// Geometry
#include "../../util/geom/ray.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <cstddef>
#include <optional>
#include <unordered_map>


namespace voxel::edit
{
	/** Level-0 sections to read in place of the world's, keyed by World::sectionIndex(0, ...). */
	using SectionOverrides = std::unordered_map<std::size_t, Section>;

	struct RaycastOptions
	{
		float		maxDistance_	{ 2000.f };		// metres

		// A material the ray passes through as if it were air; kAir for none.
		// Lets a brush reach the lake bed instead of stopping on the water.
		MaterialId	passThrough_	{ kAir };

		// Sections as they were at some earlier moment. A brush aims at the
		// terrain as its stroke found it: aimed at the live terrain, a stroke
		// that adds ground hits what it has just added, which is nearer the
		// eye, and climbs towards the camera stamp by stamp.
		const SectionOverrides*	overrides_	{ nullptr };
	};

	struct VoxelHit
	{
		glm::ivec3	voxel_		{ 0 };		// the solid voxel that was hit
		glm::ivec3	normal_		{ 0 };		// of the face the ray entered through; zero if it started inside
		glm::vec3	point_		{ 0.f };	// world space
		float		distance_	{ 0.f };	// metres
		MaterialId	material_	{ kAir };
	};

	/*!
		@brief
			Walks a ray through level 0 of the voxel grid and reports the first solid voxel.

			Sections that are uniformly empty are crossed in one step. Reads the
			grid itself, so it sees terrain the moment it is written - the physics
			raycast only sees sections whose collision mesh has been cooked and
			added - and it costs nothing to set up, which is what an editor brush
			needs every frame. Read-only: safe whenever no write is in flight (see
			voxel::System).
		@param world
			World to read
		@param ray
			Ray in world space
		@param options
			Maximum distance, pass-through material and section overrides
		@return
			The hit, or nullopt when the ray hits nothing within range
		*/
	std::optional<VoxelHit> raycast(const World& world, const util::geom::Ray& ray, const RaycastOptions& options = {});
}
