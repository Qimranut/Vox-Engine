/*************************************************************************************************************
\file       action_state.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      ActionState (one action's resolved value and edges for a frame) and Candidate (one binding's
            contribution before merging).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Bindings
#include "binding.hpp"

// GLM
#include <glm/glm.hpp>


namespace input::action
{
	/**
	 * One action's resolved value for one frame.
	 *
	 */
	struct ActionState
	{
		// Components not used by the action's ValueType stay zero. A Button
		// carries 1.0 in x while actuated.
		glm::vec3	value_		{ 0.f };

		bool		down_		{ false };
		bool		pressed_	{ false };
		bool		released_	{ false };

		// Device that produced the winning value. Drives cursor capture and
		// on-screen button glyphs.
		Device		lastDevice_	{ Device::None };

		// Map that resolved it, for the action events.
		Context		context_	{ Context::Gameplay };

		/*!
			@brief
				Resets the value and the down / pressed / released flags for a new frame.

				lastDevice_ and context_ are kept.
			*/
		void clearFrame()
		{
			value_ = { 0.f, 0.f, 0.f };
			down_ = false;
			pressed_ = false;
			released_ = false;
		}
	};

	/**
	 * One binding's contribution before merging.
	 *
	 */
	struct Candidate
	{
		glm::vec3	value_		{ 0.f };
		bool		actuated_	{ false };
		Device		device_		{ Device::None };
	};
}
