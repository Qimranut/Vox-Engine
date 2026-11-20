/*************************************************************************************************************
\file       action_map.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Action resolution: evaluates a binding against the devices, tracks and claims consumed
            sources for blocking maps, merges candidates into action state, and builds the default
            binding table from VOX_DEFAULT_BINDING_LIST.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "action_map.hpp"

// Default binding table
#include "../definitions/default_bindings.hpp"

// stds
#include <cmath>
#include <algorithm>


namespace input::action
{
	namespace
	{
		/*!
			@brief
				Reads one button off whichever device a binding names.

				A negative code means "nothing bound to this direction", which is
				how a composite covers only some of its axes.
			@param d
				Device the code belongs to
			@param code
				Button code on that device; negative for none
			@param view
				Current device state
			@return
				True if the button is held and its device is present
			*/
		bool buttonDown(Device d, int code, const DeviceView& view)
		{
			if (code < 0)
				return false;

			switch (d)
			{
			case Device::Keyboard:
				return view.keyboard_ && view.keyboard_->down(code);

			case Device::Mouse:
				return view.mouse_ && view.mouse_->down(code);

			case Device::Gamepad:
				return view.pad_ && view.pad_->down(code);

			default:
				return false;
			}
		}

		/*!
			@brief
				Reads a pair of opposing buttons as one axis.
			@param d
				Device both codes belong to
			@param pos
				Button for the positive direction
			@param neg
				Button for the negative direction
			@param view
				Current device state
			@return
				+1 for the positive button, -1 for the negative, 0 for both or
				neither
			*/
		float buttonPair(Device d, int pos, int neg, const DeviceView& view)
		{
			float v = 0.f;

			if (buttonDown(d, pos, view))
				v += 1.f;

			if (buttonDown(d, neg, view))
				v -= 1.f;

			return v;
		}

		/*!
			@brief
				Whether a binding's modifier chord is satisfied.
			@param b
				Binding whose required modifiers to test
			@param view
				Current device state
			@return
				True if no modifiers are required or all of them are held
			*/
		bool modsSatisfied(const Binding& b, const DeviceView& view)
		{
			if (b.requiredMods_ == keys::Mod::Default)
				return true;

			return view.keyboard_ && view.keyboard_->modsHeld(b.requiredMods_);
		}

		/*!
			@brief
				Whether a pad axis belongs to the left or the right stick.
			@param axis
				keys::PadAxis value as an int
			@param wantLeft
				True to test for the left stick, false for the right
			@return
				True if the axis is an X or Y axis of the requested stick
			*/
		bool isStickAxis(int axis, bool wantLeft)
		{
			const auto a = static_cast<keys::PadAxis>(axis);

			return wantLeft ? (a == keys::PadAxis::LeftX || a == keys::PadAxis::LeftY)
							: (a == keys::PadAxis::RightX || a == keys::PadAxis::RightY);
		}

		/*!
			@brief
				Whether a pad axis is one of the analog triggers.
			@param axis
				keys::PadAxis value as an int
			@return
				True for the left or right trigger axis
			*/
		bool isTriggerAxis(int axis)
		{
			const auto a = static_cast<keys::PadAxis>(axis);

			return a == keys::PadAxis::LeftTriggerAxis
				|| a == keys::PadAxis::RightTriggerAxis;
		}

		/*!
			@brief
				Whether a pad button is one of the virtual trigger buttons synthesized from the trigger axes.
			@param button
				keys::PadButton value as an int
			@return
				True for LeftTrigger or RightTrigger
			*/
		bool isVirtualTrigger(int button)
		{
			const auto b = static_cast<keys::PadButton>(button);

			return b == keys::PadButton::LeftTrigger
				|| b == keys::PadButton::RightTrigger;
		}
	}

	/*!
		@brief
			Removes every binding driving one action.
		@param a
			Action to unbind
		*/
	void ActionMap::clearBindings(Action a)
	{
		bindings_.erase(
			std::remove_if(bindings_.begin(), bindings_.end(),
				[a](const Binding& b) { return b.action_ == a; }),
			bindings_.end());
	}

	/*!
		@brief
			Evaluates one binding against the devices.

			Deadzones and scale are applied per source kind; inversion is applied
			last so the deadzone band stays symmetric (sticks invert inside
			resolveStick instead).
		@param b
			Binding to evaluate
		@param view
			Current device state
		@return
			The binding's contribution, zeroed when its device is absent
		*/
	Candidate evaluate(const Binding& b, const DeviceView& view)
	{
		Candidate c;
		c.device_ = b.device_;

		if (!modsSatisfied(b, view))
			return c;

		switch (b.kind_)
		{
		case SourceKind::KeyButton:
		case SourceKind::MouseButton:
		case SourceKind::PadButton:
		{
			const bool isDown = buttonDown(b.device_, b.code_[0], view);

			c.value_.x = isDown ? b.scale_ : 0.f;
			c.actuated_ = isDown;
			break;
		}

		case SourceKind::Axis1FromButtons:
		{
			c.value_.x = buttonPair(b.device_, b.code_[0], b.code_[1], view) * b.scale_;
			c.actuated_ = std::fabs(c.value_.x) > b.actuation_;
			break;
		}

		case SourceKind::Axis2FromButtons:
		{
			c.value_.x = buttonPair(b.device_, b.code_[0], b.code_[1], view) * b.scale_;
			c.value_.y = buttonPair(b.device_, b.code_[2], b.code_[3], view) * b.scale_;
			c.actuated_ = glm::length(glm::vec2(c.value_)) > b.actuation_;
			break;
		}

		case SourceKind::Axis3FromButtons:
		{
			c.value_.x = buttonPair(b.device_, b.code_[0], b.code_[1], view) * b.scale_;
			c.value_.y = buttonPair(b.device_, b.code_[2], b.code_[3], view) * b.scale_;
			c.value_.z = buttonPair(b.device_, b.code_[4], b.code_[5], view) * b.scale_;
			c.actuated_ = glm::length(c.value_) > b.actuation_;
			break;
		}

		case SourceKind::PadAxis1:
		{
			if (!view.pad_)
				break;

			const float raw = view.pad_->axisRaw(b.code_[0]);

			c.value_.x = applyScalarDeadzone(raw, b.innerDeadzone_, b.outerDeadzone_)
				* b.scale_;
			c.actuated_ = std::fabs(c.value_.x) > b.actuation_;
			break;
		}

		case SourceKind::PadTrigger1:
		{
			if (!view.pad_)
				break;

			// Convert BEFORE the deadzone: the raw axis rests at -1, so a
			// deadzone applied to it would read as a permanently held
			// trigger pulled backwards.
			const float travel = device::triggerToUnit(view.pad_->axisRaw(b.code_[0]));

			c.value_.x = applyScalarDeadzone(travel, b.innerDeadzone_, b.outerDeadzone_)
				* b.scale_;
			c.actuated_ = c.value_.x > b.actuation_;
			break;
		}

		case SourceKind::PadStick2:
		{
			if (!view.pad_)
				break;

			const glm::vec2 raw
			{
				view.pad_->axisRaw(b.code_[0]),
				view.pad_->axisRaw(b.code_[1])
			};

			c.value_ = resolveStick(b, raw);
			c.actuated_ = glm::length(c.value_) > b.actuation_;

			// resolveStick has already inverted, before routing the second
			// axis to z for three-component actions. Inverting again below
			// would flip the wrong component.
			return c;
		}

		case SourceKind::MouseDelta2:
		{
			if (!view.mouse_)
				break;

			const glm::vec2 d = view.mouse_->delta() * b.scale_;

			c.value_.x = d.x;
			c.value_.y = d.y;

			// Pixels, not a normalized axis: any movement at all counts.
			c.actuated_ = (d.x != 0.f || d.y != 0.f);
			break;
		}

		case SourceKind::MouseScroll1:
		{
			if (!view.mouse_)
				break;

			c.value_.x = view.mouse_->scroll().y * b.scale_;
			c.actuated_ = c.value_.x != 0.f;
			break;
		}
		}

		// Inverts last, so the deadzone band stays symmetric around zero.
		if (b.invertX_)
			c.value_.x = -c.value_.x;

		if (b.invertY_)
			c.value_.y = -c.value_.y;

		return c;
	}

	/*!
		@brief
			Whether every source a binding reads is still unclaimed.
		@param b
			Binding to test
		@param consumed
			Sources already claimed this frame
		@return
			false when a higher-priority blocking map already took one of them
		*/
	bool isAvailable(const Binding& b, const ConsumedSet& consumed)
	{
		const auto buttonFree = [&](int code) -> bool
		{
			if (code < 0)
				return true;

			switch (b.device_)
			{
			case Device::Keyboard:
				return !consumed.keys_.test(static_cast<std::size_t>(code));

			case Device::Mouse:
				return !consumed.mouse_.test(static_cast<std::size_t>(code));

			case Device::Gamepad:
				return !consumed.pad_.test(static_cast<std::size_t>(code));

			default:
				return true;
			}
		};

		switch (b.kind_)
		{
		case SourceKind::KeyButton:
		case SourceKind::MouseButton:
			return buttonFree(b.code_[0]);

		case SourceKind::PadButton:
			return buttonFree(b.code_[0])
				&& (!isVirtualTrigger(b.code_[0]) || !consumed.triggers_);

		case SourceKind::Axis1FromButtons:
			return buttonFree(b.code_[0]) && buttonFree(b.code_[1]);

		case SourceKind::Axis2FromButtons:
			return buttonFree(b.code_[0]) && buttonFree(b.code_[1])
				&& buttonFree(b.code_[2]) && buttonFree(b.code_[3]);

		case SourceKind::Axis3FromButtons:
			return buttonFree(b.code_[0]) && buttonFree(b.code_[1])
				&& buttonFree(b.code_[2]) && buttonFree(b.code_[3])
				&& buttonFree(b.code_[4]) && buttonFree(b.code_[5]);

		case SourceKind::PadAxis1:
		case SourceKind::PadStick2:
			return !(isStickAxis(b.code_[0], true) && consumed.leftStick_)
				&& !(isStickAxis(b.code_[0], false) && consumed.rightStick_);

		case SourceKind::PadTrigger1:
			return !consumed.triggers_;

		case SourceKind::MouseDelta2:
			return !consumed.mouseDelta_;

		case SourceKind::MouseScroll1:
			return !consumed.scroll_;

		default:
			return true;
		}
	}

	/*!
		@brief
			Marks every source a binding reads as claimed.
		@param b
			Binding whose sources to claim
		@param consumed
			Set to mark
		*/
	void consumeSources(const Binding& b, ConsumedSet& consumed)
	{
		const auto takeButton = [&](int code)
		{
			if (code < 0)
				return;

			switch (b.device_)
			{
			case Device::Keyboard:
				consumed.keys_.set(static_cast<std::size_t>(code));
				break;

			case Device::Mouse:
				consumed.mouse_.set(static_cast<std::size_t>(code));
				break;

			case Device::Gamepad:
				consumed.pad_.set(static_cast<std::size_t>(code));

				if (isVirtualTrigger(code))
					consumed.triggers_ = true;

				break;

			default:
				break;
			}
		};

		switch (b.kind_)
		{
		case SourceKind::KeyButton:
		case SourceKind::MouseButton:
		case SourceKind::PadButton:
			takeButton(b.code_[0]);
			break;

		case SourceKind::Axis1FromButtons:
			takeButton(b.code_[0]);
			takeButton(b.code_[1]);
			break;

		case SourceKind::Axis2FromButtons:
			for (int i = 0; i < 4; ++i)
				takeButton(b.code_[i]);
			break;

		case SourceKind::Axis3FromButtons:
			for (int i = 0; i < 6; ++i)
				takeButton(b.code_[i]);
			break;

		case SourceKind::PadAxis1:
		case SourceKind::PadStick2:
			if (isStickAxis(b.code_[0], true))
				consumed.leftStick_ = true;
			else if (isStickAxis(b.code_[0], false))
				consumed.rightStick_ = true;
			else if (isTriggerAxis(b.code_[0]))
				consumed.triggers_ = true;
			break;

		case SourceKind::PadTrigger1:
			consumed.triggers_ = true;
			break;

		case SourceKind::MouseDelta2:
			consumed.mouseDelta_ = true;
			break;

		case SourceKind::MouseScroll1:
			consumed.scroll_ = true;
			break;
		}
	}

	/*!
		@brief
			Folds a binding's contribution into an action's state.

			Merges per component by larger magnitude, never by sum, and credits the
			device that actually drove the action.
		@param state
			Action state to fold into
		@param cand
			Contribution to merge
		*/
	void mergeCandidate(ActionState& state, const Candidate& cand)
	{
		bool tookAny = false;

		for (int i = 0; i < 3; ++i)
		{
			if (std::fabs(cand.value_[i]) > std::fabs(state.value_[i]))
			{
				state.value_[i] = cand.value_[i];
				tookAny = true;
			}
		}

		if (cand.actuated_)
		{
			// The device credited is the one that actually drove the action,
			// so cursor capture and glyph hints follow the hand on the
			// control rather than whichever binding was declared first.
			if (!state.down_ || tookAny)
				state.lastDevice_ = cand.device_;

			state.down_ = true;
		}
	}

	/*!
		@brief
			The compile-time default binding table, built once on first call.
		@return
			Every entry of VOX_DEFAULT_BINDING_LIST, in declaration order
		*/
	const std::vector<Binding>& defaultBindings()
	{
		// Built once from VOX_DEFAULT_BINDING_LIST. The entry macros are
		// defined only for the length of this initialiser so they cannot leak
		// into anything that includes default_bindings.hpp later.
		static const std::vector<Binding> table =
		{
#define B_BTN(a, dev, code) \
			btn(Action::a, Device::dev, static_cast<int>(code)),
#define B_AXIS1B(a, dev, pos, neg) \
			axis1b(Action::a, Device::dev, static_cast<int>(pos), static_cast<int>(neg)),
#define B_AXIS3B(a, dev, px, nx, py, ny, pz, nz) \
			axis3b(Action::a, Device::dev, static_cast<int>(px), static_cast<int>(nx), \
			       static_cast<int>(py), static_cast<int>(ny), \
			       static_cast<int>(pz), static_cast<int>(nz)),
#define B_STICK(a, ax, ay, ix, iy) \
			stick(Action::a, static_cast<int>(ax), static_cast<int>(ay), ix, iy),
#define B_PADAX(a, ax, inv) \
			padAxis(Action::a, static_cast<int>(ax), inv),
#define B_PADTRG(a, ax) \
			padTrigger(Action::a, static_cast<int>(ax)),
#define B_MDELTA(a) \
			mouseDelta(Action::a),
#define B_SCROLL(a) \
			mouseScroll(Action::a),

			VOX_DEFAULT_BINDING_LIST

#undef B_BTN
#undef B_AXIS1B
#undef B_AXIS3B
#undef B_STICK
#undef B_PADAX
#undef B_PADTRG
#undef B_MDELTA
#undef B_SCROLL
		};

		return table;
	}
}
