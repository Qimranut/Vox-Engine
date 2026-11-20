/*************************************************************************************************************
\file       terrain_collider.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Turns a level-0 section's quads into outward-wound triangles cooked as a Jolt static mesh,
            and picks the section's friction from its dominant material.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "terrain_collider.hpp"

// Voxel
#include "../types/quad.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <array>
#include <vector>


namespace voxel
{
	/*!
		@brief
			Cooks a level-0 section mesh into an outward-wound Jolt collision mesh; worker-thread safe.
		@param mesh
			Level-0 section mesh
		@param voxelSize
			Voxel edge length in metres
		@return
			nullptr for an empty mesh or a Jolt failure
		*/
	std::shared_ptr<const physics::CookedMesh> cookSectionCollider(const SectionMesh& mesh, float voxelSize)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		if (mesh.empty())
			return nullptr;

		std::vector<glm::vec3> vertices;
		std::vector<std::uint32_t> indices;
		vertices.reserve(mesh.quads_.size() * 4);
		indices.reserve(mesh.quads_.size() * 6);

		for (const Quad& q : mesh.quads_)
		{
			const int axis = q.axis();
			const int side = q.side();
			const int u0 = q.u(), v0 = q.v(), d = q.d() + side;
			const int length = q.length(), width = q.width();

			// Corner bit0 = u, bit1 = v. Same winding rule as VoxelQuad.vert.
			const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
			for (int corner = 0; corner < 4; ++corner)
			{
				const int cu = corner & 1, cv = corner >> 1;
				const glm::ivec3 p = unswizzle(axis, u0 + cu * length, v0 + cv * width, d);
				vertices.push_back(glm::vec3(p) * voxelSize);
			}

			const bool flip = (axis == 0) ? (side == 1) : (side == 0);
			std::array<std::uint32_t, 6> tri{ 0, 1, 2, 2, 1, 3 };
			if (flip)
				tri = { 0, 2, 1, 1, 2, 3 };

			for (const std::uint32_t i : tri)
				indices.push_back(base + i);
		}

		return physics::cookStaticMesh(vertices, indices);
	}

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
	float sectionFriction(const SectionMesh& mesh, const MaterialTable& materials)
	{
		std::array<std::uint32_t, 256> count{};
		for (const Quad& q : mesh.quads_)
			++count[q.material() & 0xFF];

		std::size_t best = 0;
		for (std::size_t i = 1; i < count.size(); ++i)
			if (count[i] > count[best])
				best = i;

		return materials.get(static_cast<MaterialId>(best)).friction_;
	}
}
