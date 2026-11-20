#pragma once

// Components
#include "../component_types/transform.hpp"
#include "../component_types/visibility.hpp"
#include "../component_types/rigid_body.hpp"
#include "../component_types/collider.hpp"
#include "../component_types/character_body.hpp"
#include "../component_types/audio_source.hpp"
#include "../component_types/render_material.hpp"
#include "../component_types/voxel_world.hpp"

// Field list
#include "comp_field_list.hpp"


#define COMPONENT_LIST(X)									\
	X(cpn::Transform, TRANSFORM_FIELDS)						\
	X(cpn::Visibility, VISIBILITY_FIELDS)					\
	X(cpn::RigidBody, RIGIDBODY_FIELDS)						\
	X(cpn::Collider, COLLIDER_FIELDS)						\
	X(cpn::RenderMaterial, RENDER_MAT_FIELDS)				\
	X(cpn::CharacterBody, CHARACTER_BODY_FIELDS)			\
	X(cpn::AudioSource, AUDIO_SOURCE_FIELDS)				\
	X(cpn::VoxelWorld, VOXEL_WORLD_FIELDS)
