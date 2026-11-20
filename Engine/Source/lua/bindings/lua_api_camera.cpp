/*************************************************************************************************************
\file       lua_api_camera.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Lua Camera table: position, yaw / pitch, field of view, clip planes, camera mode, orbit target
            and distance, and LookAt. Angles cross in degrees.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "lua_api.hpp"

// Engine
#include "../interface/lua_interface.hpp"
#include "../../graphics/sys/gph_sys.hpp"

// GLM
#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

// stds
#include <string>
#include <tuple>


namespace scripting::lua
{
	/*!
		@brief
			Registers the Camera table; angles cross in degrees while the camera stores radians.
		@param lua
			Lua interface to register into
		@param gph
			Graphics system that owns the camera and orbit controller
		*/
	void registerCameraApi(Interface& lua, graphics::System& gph)
	{
		// ------------------ Camera API -----------------
		//
		// Angles cross this boundary in DEGREES (camera stores radians)

		lua.link(
			"Camera",
			"GetPosition",
			[&gph]()
			{
				const glm::vec3& p = gph.camera().position();
				return std::make_tuple(p.x, p.y, p.z);
			});

		lua.link(
			"Camera",
			"SetPosition",
			[&gph](float x, float y, float z)
			{
				gph.camera().setPosition({ x, y, z });
			});

		lua.link(
			"Camera",
			"GetYawPitch",
			[&gph]()
			{
				const graphics::camera::Camera& c = gph.camera();
				return std::make_tuple(glm::degrees(c.yaw()), glm::degrees(c.pitch()));
			});

		lua.link(
			"Camera",
			"SetYawPitch",
			[&gph](float yawDeg, float pitchDeg)
			{
				gph.camera().setYawPitch(glm::radians(yawDeg), glm::radians(pitchDeg));
			});

		lua.link(
			"Camera",
			"GetFov",
			[&gph]()
			{
				return glm::degrees(gph.camera().fovY());
			});

		lua.link(
			"Camera",
			"SetFov",
			[&gph](float fovDeg)
			{
				gph.camera().setFovY(glm::radians(fovDeg));
			});

		lua.link(
			"Camera",
			"SetClip",
			[&gph](float nearZ, float farZ)
			{
				gph.camera().setClip(nearZ, farZ);
			});

		lua.link(
			"Camera",
			"SetMode",
			[&gph](const std::string& mode)
			{
				gph.setCameraMode(mode);
			});

		lua.link(
			"Camera",
			"SetTarget",
			[&gph](float x, float y, float z)
			{
				gph.orbit().setTarget({ x, y, z });
			});

		lua.link(
			"Camera",
			"SetDistance",
			[&gph](float distance)
			{
				gph.orbit().setDesiredDistance(distance);
			});

		lua.linkOverload(
			"Camera",
			"LookAt",

			// Aim at a target from where the camera already is
			[&gph](float x, float y, float z)
			{
				graphics::camera::Camera& c = gph.camera();
				c.lookAt(c.position(), { x, y, z });
			},

			// Place the camera and aim it
			[&gph](float ex, float ey, float ez, float tx, float ty, float tz)
			{
				gph.camera().lookAt({ ex, ey, ez }, { tx, ty, tz });
			});
	}
}
