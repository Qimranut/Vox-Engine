/*************************************************************************************************************
\file       collider.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      cpn::Collider, the collision shape (box, sphere or capsule, in local space) of a
            cpn::RigidBody.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Physics types
#include "../../physics/types/physics_types.hpp"

// GLM
#include <glm/glm.hpp>


namespace cpn
{
	/**
	 * Collision shape of a cpn::RigidBody.
	 *
	 * Sizes are in LOCAL space and multiplied by Transform::scale_ when the
	 * body is built, so the defaults (a half extent of 0.5) match the unit cube
	 * the renderer draws at any scale. Scale is read once, at creation.
	 *
	 */
	struct Collider
	{
		physics::ShapeType	shape_			{ physics::ShapeType::Box };

		glm::vec3			halfExtents_	{ 0.5f };	// Box
		float				radius_			{ 0.5f };	// Sphere, Capsule
		float				halfHeight_		{ 0.5f };	// Capsule: half the cylinder part, excluding the caps

		// Shape centre relative to the entity origin.
		glm::vec3			offset_			{ 0.f };
	};
}
