#pragma once


// Component Fields

#define TRANSFORM_FIELDS(Type, F)						\
	F(Type, pos_)										\
	F(Type, rot_)										\
	F(Type, scale_)

#define VISIBILITY_FIELDS(Type, F)						\
	F(Type, visible_)

#define RIGIDBODY_FIELDS(Type, F)						\
	F(Type, motion_)									\
	F(Type, layer_)										\
	F(Type, mass_)										\
	F(Type, friction_)									\
	F(Type, restitution_)								\
	F(Type, linearDamping_)								\
	F(Type, angularDamping_)							\
	F(Type, gravityFactor_)								\
	F(Type, isSensor_)									\
	F(Type, allowSleeping_)								\
	F(Type, bodyId_)									\
	F(Type, prevPos_)									\
	F(Type, currPos_)									\
	F(Type, prevRot_)									\
	F(Type, currRot_)

#define COLLIDER_FIELDS(Type, F)						\
	F(Type, shape_)										\
	F(Type, halfExtents_)								\
	F(Type, radius_)									\
	F(Type, halfHeight_)								\
	F(Type, offset_)

#define RENDER_MAT_FIELDS(Type, F)						\
	F(Type, albedo_)

#define CHARACTER_BODY_FIELDS(Type, F)					\
	F(Type, radius_)									\
	F(Type, height_)									\
	F(Type, walkSpeed_)									\
	F(Type, sprintSpeed_)								\
	F(Type, jumpSpeed_)									\
	F(Type, groundAccel_)								\
	F(Type, airControl_)								\
	F(Type, maxSlopeDeg_)								\
	F(Type, playerControlled_)							\
	F(Type, handle_)

#define AUDIO_SOURCE_FIELDS(Type, F)					\
	F(Type, path)										\
	F(Type, channel)									\
	F(Type, volume)										\
	F(Type, loop)										\
	F(Type, playOnAwake)

#define VOXEL_WORLD_FIELDS(Type, F)						\
	F(Type, map_)

#define TEXT_FIELDS(Type, F)							\
	F(Type, font)										\
	F(Type, content)									\
	F(Type, screenSpace)								\
	F(Type, size)										\
	F(Type, screenPosition)								\
	F(Type, color)										\
	F(Type, alignment)

// Custom data type Fields

// GLM VEC2
#define GLM_VEC2_FIELD(Type, F)    \
	F(Type, x)                     \
	F(Type, y)

// GLM VEC3
#define GLM_VEC3_FIELD(Type, F)							\
	F(Type, x)											\
	F(Type, y)											\
	F(Type, z)

// GLM VEC4
#define GLM_VEC4_FIELD(Type, F)							\
	F(Type, x)											\
	F(Type, y)											\
	F(Type, z)											\
	F(Type, w)

// GLM Quat
#define GLM_QUAT_FIELD(Type, F)							\
	F(Type, x)											\
	F(Type, y)											\
	F(Type, z)											\
	F(Type, w)

// Enums

// Physics Motion Type
#define MOTION_TYPE_VALUES(Type, F)						\
	F(Type, Static)										\
	F(Type, Kinematic)									\
	F(Type, Dynamic)

// Physics Layer
#define LAYER_VALUES(Type, F)							\
	F(Type, Static)										\
	F(Type, Dynamic)									\
	F(Type, Character)									\
	F(Type, Debris)										\
	F(Type, Sensor)										\
	F(Type, Count)

// Physics Shapes
#define SHAPE_TYPE_VALUES(Type, F)						\
	F(Type, Box)										\
	F(Type, Sphere)										\
	F(Type, Capsule)