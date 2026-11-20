/*************************************************************************************************************
\file       lua_api_voxel.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Lua Voxel table: world state and size, material get / set and name lookups, FillSphere and
            FillBox bulk edits, surface height, grid raycast, hit-to-voxel and voxel centre queries.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "lua_api.hpp"
#include "lua_api_util.hpp"

// Lua
#include "../interface/lua_interface.hpp"

// Voxel
#include "../../voxel/sys/voxel_sys.hpp"

// stds
#include <string>
#include <tuple>


namespace scripting::lua
{
	/*!
		@brief
			Registers the Voxel table: world queries, material lookups, raycasts and queued edits.

			Voxel coordinates cross as integers (0-based, +Y up); material ids as
			integers with 0 = air. Voxel.Raycast finds the voxel under a ray from
			the grid; Physics.Raycast + Voxel.HitToVoxel does it from the collision
			mesh, which only exists where and when colliders are cooked.

			SetMaterial is queued: the section remeshes and its collision body
			rebuilds over the next frame or two, never inside this call.
		@param lua
			Lua interface to register into
		@param vox
			The voxel system
		*/
	void registerVoxelApi(Interface& lua, voxel::System& vox)
	{
		lua.link("Voxel", "IsReady", [&vox]() { return vox.ready(); });

		lua.link("Voxel", "HasWorld", [&vox]() { return vox.hasWorld(); });

		lua.link("Voxel", "Entity", [&vox]() { return toLuaEntity(vox.entity()); });

		lua.link("Voxel", "VoxelSize", [&vox]() { return vox.voxelSize(); });

		lua.link("Voxel", "WorldSize", [&vox]()
		{
			const glm::ivec3 s = vox.worldSizeVoxels();
			return std::make_tuple(s.x, s.y, s.z);
		});

		lua.link("Voxel", "Origin", [&vox]()
		{
			const glm::vec3 o = vox.origin();
			return std::make_tuple(o.x, o.y, o.z);
		});

		lua.link("Voxel", "GetMaterial", [&vox](int x, int y, int z)
		{
			return static_cast<int>(vox.material(x, y, z));
		});

		lua.link("Voxel", "SetMaterial", [&vox](int x, int y, int z, int material)
		{
			if (material < 0 || material > 255)
				return false;
			return vox.setMaterial(x, y, z, static_cast<voxel::MaterialId>(material));
		});

		// Whole shapes at once, queued like SetMaterial but applied a section
		// at a time: the way to blast a crater, not a loop over SetMaterial.
		// Material 0 empties the shape. Coordinates and radius are in voxels.
		lua.link("Voxel", "FillSphere", [&vox](int x, int y, int z, float radius, int material)
		{
			if (material < 0 || static_cast<std::size_t>(material) >= vox.materials().size() || radius <= 0.f || !vox.editable())
				return false;

			voxel::edit::Stamp stamp;
			stamp.centre_ = glm::vec3(x, y, z) + 0.5f;
			stamp.half_ = glm::vec3{ radius };
			stamp.shape_ = voxel::edit::Shape::Sphere;

			voxel::edit::OpParams params;
			params.op_ = voxel::edit::Op::Fill;
			params.material_ = static_cast<voxel::MaterialId>(material);
			vox.queueBrush({ stamp }, params);
			return true;
		});

		// Both corners inclusive.
		lua.link("Voxel", "FillBox", [&vox](int x0, int y0, int z0, int x1, int y1, int z1, int material)
		{
			if (material < 0 || static_cast<std::size_t>(material) >= vox.materials().size() || !vox.editable())
				return false;

			const glm::vec3 lo = glm::vec3(glm::min(glm::ivec3{ x0, y0, z0 }, glm::ivec3{ x1, y1, z1 }));
			const glm::vec3 hi = glm::vec3(glm::max(glm::ivec3{ x0, y0, z0 }, glm::ivec3{ x1, y1, z1 })) + 1.f;

			voxel::edit::Stamp stamp;
			stamp.centre_ = 0.5f * (lo + hi);
			stamp.half_ = 0.5f * (hi - lo);
			stamp.shape_ = voxel::edit::Shape::Cube;

			voxel::edit::OpParams params;
			params.op_ = voxel::edit::Op::Fill;
			params.material_ = static_cast<voxel::MaterialId>(material);
			vox.queueBrush({ stamp }, params);
			return true;
		});

		lua.link("Voxel", "MaterialName", [&vox](int id)
		{
			if (id < 0 || static_cast<std::size_t>(id) >= vox.materials().size())
				return std::string{};
			return vox.materials().get(static_cast<voxel::MaterialId>(id)).name_;
		});

		lua.link("Voxel", "MaterialId", [&vox](const std::string& name)
		{
			const voxel::MaterialId id = vox.materials().id(name);
			return id == voxel::kUnknownMaterial ? -1 : static_cast<int>(id);
		});

		lua.link("Voxel", "MaterialCount", [&vox]() { return static_cast<int>(vox.materials().size()); });

		// Highest solid voxel in a column, or -1.
		lua.link("Voxel", "SurfaceHeight", [&vox](int x, int z)
		{
			const std::optional<int> h = vox.surfaceHeight(x, z);
			return h ? *h : -1;
		});

		lua.link("Voxel", "HitToVoxel", [&vox](float px, float py, float pz, float nx, float ny, float nz)
		{
			const glm::ivec3 v = vox.hitToVoxel({ px, py, pz }, { nx, ny, nz });
			return std::make_tuple(v.x, v.y, v.z);
		});

		// First solid voxel along a ray, read from the voxel grid itself: it
		// needs no collision mesh (the Editor cooks none until Play) and sees
		// an edit the frame it lands. Returns hit, the voxel, the normal of the
		// face entered, the hit point, the distance and the material id.
		lua.link("Voxel", "Raycast", [&vox](float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance)
		{
			using Result = std::tuple<bool, int, int, int, int, int, int, float, float, float, float, int>;

			const float length = glm::length(glm::vec3{ dx, dy, dz });
			if (!(length > 0.f) || !(maxDistance > 0.f))
				return Result{};

			voxel::edit::RaycastOptions options;
			options.maxDistance_ = maxDistance;

			const auto hit = vox.raycast({ { ox, oy, oz }, glm::vec3{ dx, dy, dz } / length }, options);
			if (!hit)
				return Result{};

			return Result{ true,
				hit->voxel_.x, hit->voxel_.y, hit->voxel_.z,
				hit->normal_.x, hit->normal_.y, hit->normal_.z,
				hit->point_.x, hit->point_.y, hit->point_.z,
				hit->distance_, static_cast<int>(hit->material_) };
		});

		// World-space centre of a voxel, for placing pickups where a voxel was.
		lua.link("Voxel", "VoxelCentre", [&vox](int x, int y, int z)
		{
			const float s = vox.voxelSize();
			const glm::vec3 c = vox.origin() + (glm::vec3(x, y, z) + 0.5f) * s;
			return std::make_tuple(c.x, c.y, c.z);
		});
	}
}
