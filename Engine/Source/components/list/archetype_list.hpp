/*************************************************************************************************************
\file       archetype_list.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      ARCHETYPE_LIST, the X-macro list of every archetype and its prototype components,
            consumed by ArchetypeManager::registerArchetypes().

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

#include "comp_list.hpp"
#include "archetype_defaults.hpp"


// Values containing a comma must come from archetype_defaults.hpp - see the
// note at the top of that file.
#define ARCHETYPE_LIST														\
		X(Default,															\
		X_COMPONENT(cpn::Transform,		cpn::Transform{})					\
		X_COMPONENT(cpn::Visibility,	cpn::Visibility{}))					\
																			\
		X(StaticBox,														\
		X_COMPONENT(cpn::Transform,		cpn::Transform{})					\
		X_COMPONENT(cpn::Visibility,	cpn::defaults::staticTint())		\
		X_COMPONENT(cpn::RigidBody,		cpn::defaults::staticBody())		\
		X_COMPONENT(cpn::Collider,		cpn::Collider{}))					\
																			\
		X(DynamicBox,														\
		X_COMPONENT(cpn::Transform,		cpn::Transform{})					\
		X_COMPONENT(cpn::Visibility,	cpn::defaults::dynamicTint())		\
		X_COMPONENT(cpn::RigidBody,		cpn::defaults::dynamicBody())		\
		X_COMPONENT(cpn::Collider,		cpn::Collider{}))					\
																			\
		X(SensorBox,														\
		X_COMPONENT(cpn::Transform,		cpn::Transform{})					\
		X_COMPONENT(cpn::RigidBody,		cpn::defaults::sensorBody())		\
		X_COMPONENT(cpn::Collider,		cpn::Collider{}))					\
																			\
		X(Player,															\
		X_COMPONENT(cpn::Transform,		cpn::Transform{})					\
		X_COMPONENT(cpn::Visibility,	cpn::defaults::playerTint())		\
		X_COMPONENT(cpn::CharacterBody,	cpn::defaults::playerCharacter()))	\

