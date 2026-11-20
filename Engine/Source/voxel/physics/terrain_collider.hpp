/*************************************************************************************************************
\file       terrain_collider.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the conversion of a level-0 section mesh into a Jolt collision mesh, and the
            section's friction.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../mesh/section_mesh.hpp"
#include "../types/material_table.hpp"

// Physics
#include "../../physics/shape/cooked_mesh.hpp"

// stds
#include <memory>


namespace voxel
{
	/*!
		@brief
			Cooks a level-0 section mesh into a collision shape.

			Every quad becomes two triangles in metres local to the section
			origin, wound outward so Jolt's surface normal points out of the
			terrain (the extraction attack reads that normal).

			Worker-thread safe.
		@param mesh
			Level-0 section mesh
		@param voxelSize
			Voxel edge length in metres
		@return
			nullptr for an empty mesh or a Jolt failure
		*/
	std::shared_ptr<const physics::CookedMesh> cookSectionCollider(const SectionMesh& mesh, float voxelSize);

	/*!
		@brief
			Friction for a section: that of the material owning most quads.
		@param mesh
			Section mesh
		@param materials
			Material table holding the friction values
		@return
			The friction coefficient
		*/
	float sectionFriction(const SectionMesh& mesh, const MaterialTable& materials);
}
