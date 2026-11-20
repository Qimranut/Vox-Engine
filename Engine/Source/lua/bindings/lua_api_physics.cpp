/*************************************************************************************************************
\file       lua_api_physics.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Lua Physics table: Raycast, gravity, impulses and velocity, contact and trigger callbacks, and
            Off. Entities cross as integers (0xFFFFFFFF = none).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "lua_api.hpp"

// Lua
#include "../interface/lua_interface.hpp"
#include "lua_input_events.hpp"
#include "lua_api_util.hpp"

// Physics
#include "../../physics/sys/phy_sys.hpp"
#include "../../physics/events/physics_events.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <cstdint>
#include <string>
#include <tuple>


namespace scripting::lua
{
	/*!
		@brief
			Registers the Physics table.

			Physics.On* callbacks run inside the fixed timestep, so a frame may deliver several or none.
		@param lua
			Lua interface to register into
		@param bridge
			Owner of the Lua contact and trigger subscriptions (same handle space as Input.On*)
		@param phys
			The physics system
		*/
	void registerPhysicsApi(Interface& lua, InputEventBridge& bridge, physics::System& phys)
	{
		// ----------------- Physics API -----------------
		//
		// Entities cross this boundary as integers (0xFFFFFFFF = none).
		// Physics.On* callbacks run inside the fixed timestep, so a frame may
		// deliver several or none.

		lua.link(
			"Physics",
			"Raycast",
			[&phys](float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance)
			{
				const auto hit = phys.raycast({ ox, oy, oz }, { dx, dy, dz }, maxDistance);

				if (!hit)
					return std::make_tuple(false, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, toLuaEntity(entt::null));

				return std::make_tuple(true,
					hit->point_.x, hit->point_.y, hit->point_.z,
					hit->normal_.x, hit->normal_.y, hit->normal_.z,
					toLuaEntity(hit->entity_));
			});

		lua.link(
			"Physics",
			"SetGravity",
			[&phys](float x, float y, float z)
			{
				phys.setGravity({ x, y, z });
			});

		lua.link(
			"Physics",
			"GetGravity",
			[&phys]()
			{
				const glm::vec3 g = phys.gravity();
				return std::make_tuple(g.x, g.y, g.z);
			});

		lua.link(
			"Physics",
			"AddImpulse",
			[&phys](std::uint32_t entity, float x, float y, float z)
			{
				phys.addImpulse(fromLuaEntity(entity), { x, y, z });
			});

		lua.link(
			"Physics",
			"SetVelocity",
			[&phys](std::uint32_t entity, float x, float y, float z)
			{
				phys.setLinearVelocity(fromLuaEntity(entity), { x, y, z });
			});

		// OnContact("Began", fn(a, b, px, py, pz, approachSpeed))
		// OnContact("Ended", fn(a, b))
		lua.link(
			"Physics",
			"OnContact",
			[&bridge](std::string phase, sol::protected_function fn)
			{
				namespace pev = physics::events;

				if (phase == "Began")
				{
					return bridge.subscribe<pev::ContactBegan>(
						[fn](const pev::ContactBegan& e)
						{
							InputEventBridge::call(fn, toLuaEntity(e.a_), toLuaEntity(e.b_),
								e.point_.x, e.point_.y, e.point_.z, e.approachSpeed_);
						});
				}

				if (phase == "Ended")
				{
					return bridge.subscribe<pev::ContactEnded>(
						[fn](const pev::ContactEnded& e)
						{
							InputEventBridge::call(fn, toLuaEntity(e.a_), toLuaEntity(e.b_));
						});
				}

				return badPhase("Physics.OnContact", phase);
			});

		// OnTrigger("Entered" | "Exited", fn(trigger, other))
		lua.link(
			"Physics",
			"OnTrigger",
			[&bridge](std::string phase, sol::protected_function fn)
			{
				namespace pev = physics::events;

				if (phase == "Entered")
				{
					return bridge.subscribe<pev::TriggerEntered>(
						[fn](const pev::TriggerEntered& e)
						{
							InputEventBridge::call(fn, toLuaEntity(e.trigger_), toLuaEntity(e.other_));
						});
				}

				if (phase == "Exited")
				{
					return bridge.subscribe<pev::TriggerExited>(
						[fn](const pev::TriggerExited& e)
						{
							InputEventBridge::call(fn, toLuaEntity(e.trigger_), toLuaEntity(e.other_));
						});
				}

				return badPhase("Physics.OnTrigger", phase);
			});

		// Same handle space as Input.On* - either Off works.
		lua.link(
			"Physics",
			"Off",
			[&bridge](int handle)
			{
				bridge.remove(handle);
			});
	}
}
