/*************************************************************************************************************
\file       physics_test_scene.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Builds the Game-only physics demo arena (floor, wall, boxes, ramp, stairs, trigger, player),
            or with a voxel world just the player, dropped on the terrain once it is ready.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "physics_test_scene.hpp"

// Engine
#include "../systems/system_manager.hpp"

// Subsystems
#include "../../events/bus.hpp"
#include "../../ecs/ecs_manager/ecs_mng.hpp"
#include "../../graphics/sys/gph_sys.hpp"
#include "../../physics/sys/phy_sys.hpp"
#include "../../physics/events/physics_events.hpp"
#include "../../lua/bindings/lua_api_util.hpp"
#include "../../voxel/sys/voxel_sys.hpp"
#include "../../voxel/events/voxel_events.hpp"
#include "../../scene/SceneManager.hpp"

// Component registry
#include "../../components/registry/comp_registry.hpp"

// GLM
#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

// stds
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>


namespace engine::checks
{
	/*!
		@brief
			Spawns the physics demo arena and the player, and points the orbit
			camera at the player.

			With a voxel world referenced by the scene only the player is spawned,
			and it is placed on the first dry surface column on WorldReady.
		@param systems
			Fully initialised subsystems
		*/
	void buildPhysicsTestScene(SystemManager& systems)
	{
		ecs::ECSManager& ecsManager = systems.ecsManager();
		graphics::System& gph = systems.graphicsSys();
		physics::System& phys = systems.physicsSys();
		events::Bus& bus = systems.bus();

		auto& reg = ecsManager.registry();

		const auto spawn = [&ecsManager, &reg](const std::string& archetype,
											   const glm::vec3& pos,
											   const glm::vec3& scale,
											   const glm::quat& rot = glm::quat{ 1.f, 0.f, 0.f, 0.f })
		{
			const entt::entity e = ecsManager.instantiate(archetype);

			if (e == entt::null)
			{
				std::cerr << "[physics-scene] unknown archetype " << archetype << "\n";
				return e;
			}

			auto& tf = reg.get<cpn::Transform>(e);
			tf.pos_ = pos;
			tf.scale_ = scale;
			tf.rot_ = rot;

			return e;
		};

		// Test texture on the wall and the ramp only. The tint goes to white so
		// the image shows as authored instead of darkened by the grey static
		// tint. Each face shows the whole image once, stretched to the face:
		// the cube's UVs run 0..1 per face and there is no tiling yet.
		const auto texture = [&reg](entt::entity e, const char* path = "textures/testtest.png")
		{
			if (e == entt::null)
				return;

			reg.emplace_or_replace<cpn::RenderMaterial>(e, cpn::RenderMaterial{ path });
			reg.get<cpn::Visibility>(e).tint_ = glm::vec3{ 1.f };
		};

		// With a voxel world referenced by the scene the terrain is the arena:
		// the box floor would sit inside it. Only the player is spawned, and
		// it is placed on the surface once the terrain's bodies exist.
		const bool voxelWorld = !systems.scenes().world().empty();

		if (!voxelWorld)
		{
			// Arena floor and a wall to back the camera into.
			spawn("StaticBox", { 0.f, -0.5f, 0.f }, { 60.f, 1.f, 60.f });
			texture(spawn("StaticBox", { 0.f, 2.f, -8.f }, { 12.f, 4.f, 0.5f }));

			// Loose boxes to shove.
			for (int i = 0; i < 4; ++i)
				spawn("DynamicBox", { -2.f + static_cast<float>(i) * 1.5f, 0.5f, 3.f }, glm::vec3{ 0.8f });

			// Compressed texture example: a pushable brick block. The material names
			// the source PNG; CookAssets compressed it to BC7 sRGB with mips, and
			// ResourceManager loads Assets/.cooked/textures/compression/bricks.png.dds
			// in its place (see the Textures section of CLAUDE.md).
			texture(spawn("DynamicBox", { 4.f, 0.75f, 3.f }, glm::vec3{ 1.5f }), "textures/compression/bricks.png");

			// A 20 degree ramp - walkable, under the 50 degree slope limit.
			texture(spawn("StaticBox", { -6.f, 0.9f, 2.f }, { 3.f, 0.2f, 6.f },
				glm::angleAxis(glm::radians(20.f), glm::vec3{ 1.f, 0.f, 0.f })));

			// Stairs with 0.25 m risers, under the 0.4 m step-up.
			for (int i = 0; i < 6; ++i)
			{
				const float h = 0.25f * static_cast<float>(i + 1);
				spawn("StaticBox", { -12.f, 0.5f * h, -2.f - 0.5f * static_cast<float>(i) }, { 3.f, h, 0.5f });
			}

			const entt::entity e = spawn("DynamicBox", { 4.f, 0.75f, 3.f }, glm::vec3{ 1.5f });
			auto& mat = reg.get_or_emplace<cpn::RenderMaterial>(e);
			mat.albedo_ = "textures/testtest.png";
			mat.metallic_ = 0.5f;
			mat.normalMap_ = "textures/compression/bumps_n.png";
			mat.roughness_ = 0.5f;
			reg.get<cpn::Visibility>(e).tint_ = glm::vec3{ 1.f };

			// A trigger volume - invisible.
			const entt::entity sensor = spawn("SensorBox", { 8.f, 1.f, 6.f }, { 3.f, 2.f, 3.f });

			bus.subscribe<physics::events::TriggerEntered>(
				[sensor](const physics::events::TriggerEntered& e)
				{
					if (e.trigger_ == sensor)
						std::cout << "[physics-scene] trigger entered by entity " << scripting::lua::toLuaEntity(e.other_) << "\n";
				});

			bus.subscribe<physics::events::TriggerExited>(
				[sensor](const physics::events::TriggerExited& e)
				{
					if (e.trigger_ == sensor)
						std::cout << "[physics-scene] trigger exited by entity " << scripting::lua::toLuaEntity(e.other_) << "\n";
				});
		}

		// The player. Scale is visual only - the capsule size is in
		// CharacterBody - and matches its default 0.6 x 1.8.
		const glm::vec3 playerCentre = voxelWorld ? glm::vec3{ 50.f, 30.f, 50.f } : glm::vec3{ 0.f, 1.f, 6.f };
		const entt::entity player = spawn("Player", playerCentre, { 0.6f, 1.8f, 0.6f });

		if (voxelWorld)
		{
			// Terrain bodies stream in over several frames; drop the player on
			// the surface at the map's centre once they are all there.
			bus.subscribe<voxel::events::WorldReady>(
				[player, playerCentre, &systems](const voxel::events::WorldReady&)
				{
					voxel::System& vox = systems.voxelSys();
					const glm::ivec3 size = vox.worldSizeVoxels();

					// A bounded map: its centre. A streamed terrain is only there
					// round the camera, which starts out following the player, so
					// the search begins under where the player was spawned.
					glm::ivec2 start{ size.x / 2, size.z / 2 };
					if (vox.endless())
					{
						const glm::vec3 local = (playerCentre - vox.origin()) / vox.voxelSize();
						start = { std::clamp(static_cast<int>(local.x), 0, size.x - 1), std::clamp(static_cast<int>(local.z), 0, size.z - 1) };
					}

					int cx = start.x, cz = start.y;
					std::optional<int> h = vox.surfaceHeight(cx, cz);

					// The centre may be a lake. Walk outward ring by ring, in a
					// fixed order, to the first column that is not water.
					const voxel::MaterialId water = vox.materials().id("water");
					const auto wet = [&](int x, int z, const std::optional<int>& top)
					{
						return top && water != voxel::kUnknownMaterial && vox.material(x, *top, z) == water;
					};

					constexpr int kStep = 10;
					for (int ring = 1; wet(cx, cz, h) && ring * kStep < std::min(size.x, size.z) / 2; ++ring)
					{
						for (int dz = -ring; dz <= ring && wet(cx, cz, h); ++dz)
						for (int dx = -ring; dx <= ring && wet(cx, cz, h); ++dx)
						{
							if (std::max(std::abs(dx), std::abs(dz)) != ring)
								continue;
							const int x = start.x + dx * kStep, z = start.y + dz * kStep;
							const std::optional<int> top = vox.surfaceHeight(x, z);
							if (top && !wet(x, z, top))
							{
								cx = x;
								cz = z;
								h = top;
							}
						}
					}
					const float vs = vox.voxelSize();

					const glm::vec3 feet = vox.origin() + glm::vec3(
						(static_cast<float>(cx) + 0.5f) * vs,
						static_cast<float>(h.value_or(0) + 1) * vs + 0.05f,
						(static_cast<float>(cz) + 0.5f) * vs);

					// teleport() takes the capsule centre for a character.
					systems.physicsSys().teleport(player, feet + glm::vec3{ 0.f, 0.9f, 0.f });
					std::cout << "[physics-scene] player placed on the terrain at ("
						<< feet.x << ", " << feet.y << ", " << feet.z << ")\n";
				});
		}

		// Static bodies were added one at a time; rebuild the tree once.
		phys.optimizeBroadPhase();

		gph.setCameraMode(graphics::CameraMode::Orbit);
		gph.orbit().setTarget(playerCentre - glm::vec3{ 0.f, 0.9f, 0.f });
		gph.orbit().snapToTarget();
	}
}
