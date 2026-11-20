/*************************************************************************************************************
\file       jolt_glm.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      glm <-> Jolt vector and quaternion conversions for physics sources, the one safe way across
            the differing quaternion component orders.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Jolt - Jolt.h must precede every other Jolt header
#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>


// ---------------------------------------------------------------------------
//  glm <-> Jolt conversions. Physics sources only.
//
//  THE TRAP: the two quaternion constructors take their components in
//  different orders.
//
//      glm::quat(w, x, y, z)       JPH::Quat(x, y, z, w)
//
//  Passing them straight across compiles, looks plausible and rotates every
//  body about the wrong axis. Always convert through these helpers;
//  engine::checks::runPhysicsSelfCheck() round-trips a rotation to prove it.
//
//  Single precision only: JPH_DOUBLE_PRECISION is not defined, so RVec3 is
//  Vec3. If that ever changes, these need RVec3 overloads.
// ---------------------------------------------------------------------------

namespace physics
{
	/*!
		@brief
			Converts a glm vector to a Jolt vector.
		@param v
			glm vector
		@return
			The same vector as JPH::Vec3
		*/
	inline JPH::Vec3 toJolt(const glm::vec3& v)
	{
		return JPH::Vec3(v.x, v.y, v.z);
	}

	/*!
		@brief
			Converts a glm quaternion to a Jolt quaternion, reordering components.
		@param q
			glm quaternion (w, x, y, z)
		@return
			The same rotation as JPH::Quat (x, y, z, w)
		*/
	inline JPH::Quat toJolt(const glm::quat& q)
	{
		return JPH::Quat(q.x, q.y, q.z, q.w);
	}

	/*!
		@brief
			Converts a Jolt vector to a glm vector.
		@param v
			Jolt vector
		@return
			The same vector as glm::vec3
		*/
	inline glm::vec3 toGlm(JPH::Vec3Arg v)
	{
		return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
	}

	/*!
		@brief
			Converts a Jolt quaternion to a glm quaternion, reordering components.
		@param q
			Jolt quaternion (x, y, z, w)
		@return
			The same rotation as glm::quat (w, x, y, z)
		*/
	inline glm::quat toGlm(JPH::QuatArg q)
	{
		return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
	}
}
