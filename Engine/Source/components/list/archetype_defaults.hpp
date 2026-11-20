/*************************************************************************************************************
\file       archetype_defaults.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Prototype values for ARCHETYPE_LIST entries that need more than one field. They cannot be
            written inline: X_COMPONENT(cpn::RigidBody, cpn::RigidBody{ a, b }) splits at the comma
            inside the braces, because braces do not protect commas from the preprocessor. One
            function per prototype, called from the list with no arguments, keeps the list comma-free.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

#include "comp_list.hpp"


namespace cpn::defaults
{
	/*!
		@brief
			A visible Visibility with the given tint.
		@param tint
			The tint colour (sRGB).
		@return
			The Visibility value.
		*/
	inline Visibility visible(const glm::vec3& tint)
	{
		Visibility v;
		v.visible_ = true;
		v.tint_ = tint;
		return v;
	}

	/*!
		@brief
			The grey-blue tint of static boxes.
		@return
			A visible Visibility.
		*/
	inline Visibility staticTint()		{ return visible({ 0.55f, 0.55f, 0.6f }); }

	/*!
		@brief
			The warm tint of dynamic boxes.
		@return
			A visible Visibility.
		*/
	inline Visibility dynamicTint()		{ return visible({ 1.f, 0.85f, 0.6f }); }

	/*!
		@brief
			The light blue tint of the player.
		@return
			A visible Visibility.
		*/
	inline Visibility playerTint()		{ return visible({ 0.4f, 0.8f, 1.f }); }

	/*!
		@brief
			A static rigid body on the Static layer.
		@return
			The RigidBody prototype.
		*/
	inline RigidBody staticBody()
	{
		RigidBody rb;
		rb.motion_ = physics::MotionType::Static;
		rb.layer_ = physics::Layer::Static;
		return rb;
	}

	/*!
		@brief
			A dynamic rigid body on the Dynamic layer.
		@return
			The RigidBody prototype.
		*/
	inline RigidBody dynamicBody()
	{
		RigidBody rb;
		rb.motion_ = physics::MotionType::Dynamic;
		rb.layer_ = physics::Layer::Dynamic;
		return rb;
	}

	/*!
		@brief
			A static trigger volume on the Sensor layer.
		@return
			The RigidBody prototype.
		*/
	inline RigidBody sensorBody()
	{
		RigidBody rb;
		rb.motion_ = physics::MotionType::Static;
		rb.layer_ = physics::Layer::Sensor;
		rb.isSensor_ = true;
		return rb;
	}

	/*!
		@brief
			A character body that receives the player's intent.
		@return
			The CharacterBody prototype.
		*/
	inline CharacterBody playerCharacter()
	{
		CharacterBody cb;
		cb.playerControlled_ = true;
		return cb;
	}
}
