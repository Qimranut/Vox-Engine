/*************************************************************************************************************
\file       input_sys.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      input::System implementation: GLFW callbacks that only enqueue, the per-frame drain into the
            devices, gamepad polling and primary-pad election, action resolution and publishing,
            contexts, rebinding, cursor mode and the debug action log.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "input_sys.hpp"

// Optional gamepad mapping database
#include "../../util/file_io/file_io.hpp"

// GLFW
#include <GLFW/glfw3.h>

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <cmath>
#include <cassert>
#include <iostream>


namespace input
{
	namespace
	{
		// glfwSetJoystickCallback is GLOBAL - there is no window and therefore
		// no window user pointer, so there is no way to recover `this` except
		// a file-scope pointer. This is the one place the engine needs a
		// singleton and it is GLFW's API shape, not a design choice. Asserted
		// single-instance so that a second input::System fails loudly instead
		// of silently stealing pad events from the first.
		System* g_joystickOwner = nullptr;

		/*!
			@brief
				Global GLFW joystick callback; forwards to the owning input::System.
			@param jid
				Joystick slot
			@param event
				GLFW_CONNECTED or GLFW_DISCONNECTED
			*/
		void JoystickCallback(int jid, int event)
		{
			if (g_joystickOwner)
				g_joystickOwner->onJoystickEvent(jid, event);
		}

		/*!
			@brief
				Recovers the system from a window, for the per-window callbacks.
			@param win
				Window whose user pointer holds the system
			@return
				The owning input::System, or null
			*/
		System* fromWindow(GLFWwindow* win)
		{
			return static_cast<System*>(glfwGetWindowUserPointer(win));
		}
	}

	/*!
		@brief
			Registers as the joystick-callback owner, sets up the action maps and pads, and loads the default bindings.
		@param bus
			Event bus every input event is published on
		*/
	System::System(::events::Bus& bus)
		:
		bus_{ bus },
		cursorMode_{ GLFW_CURSOR_NORMAL }
	{
		assert(!g_joystickOwner && "only one input::System may exist");
		g_joystickOwner = this;

		// GLFW hands out events faster than a frame in a burst, so start with
		// room rather than growing during the drain.
		queue_.reserve(256);

		for (std::size_t i = 0; i < maps_.size(); ++i)
			maps_[i].setContext(static_cast<action::Context>(i));

		for (int jid = 0; jid < static_cast<int>(keys::kMaxGamepads); ++jid)
			pads_[static_cast<std::size_t>(jid)].setSlot(jid);

		resetBindingsToDefaults();

		// Lowest priority first. UI stays off the stack until a menu pushes
		// it, which is what lets it block the camera when it appears.
		contextStack_ = { action::Context::Gameplay,
						  action::Context::Camera,
						  action::Context::Editor };
	}

	/*!
		@brief
			Removes the global joystick callback and releases the owner slot.
		*/
	System::~System()
	{
		// A pad unplugged during teardown would otherwise call into freed
		// memory through the global callback.
		glfwSetJoystickCallback(nullptr);

		if (g_joystickOwner == this)
			g_joystickOwner = nullptr;
	}

	// ======================== Setup ========================

	/*!
		@brief
			Subscribes to window and own action events, loads pad mappings, installs the joystick callback
			and enumerates pads already present. Must run before the window is created.
		*/
	void System::subscriptionSetup()
	{
		bus_.subscribe<window::events::WindowCreated>(
			[this](const window::events::WindowCreated& e)
			{
				onWindowCreation(e);
			});

		bus_.subscribe<window::events::WindowDestroyed>(
			[this](const window::events::WindowDestroyed& e)
			{
				onWindowDestruction(e);
			});

		subscribeInternalConsumers();

		// Order matters: mappings first, so that the joystick callback and the
		// startup enumeration can both ask glfwJoystickIsGamepad and get a
		// truthful answer.
		loadGamepadMappings();

		glfwSetJoystickCallback(JoystickCallback);

		enumerateGamepads();
	}

	/*!
		@brief
			Subscribes the cursor capture policy and the debug toggle and log to the system's action events.
		*/
	void System::subscribeInternalConsumers()
	{
		// The system's own features consume its events like anyone else,
		// rather than reading its internal state behind the bus's back.

		// Cursor capture. Mouse-gated on the press only: whichever device
		// ends the hold, a captured cursor must be released.
		bus_.subscribe<events::ActionStarted>(
			[this](const events::ActionStarted& e)
			{
				if (captureWhileHeld_ != action::None
					&& e.action_ == captureWhileHeld_
					&& e.device_ == action::Device::Mouse)
				{
					setCursorCaptured(true);
				}
			});

		bus_.subscribe<events::ActionCanceled>(
			[this](const events::ActionCanceled& e)
			{
				if (captureWhileHeld_ != action::None
					&& e.action_ == captureWhileHeld_
					&& cursorCaptured())
				{
					setCursorCaptured(false);
				}
			});

		bus_.subscribe<events::ActionPerformed>(
			[this](const events::ActionPerformed& e)
			{
				if (e.action_ == action::DebugInput)
					debugLogging_ = !debugLogging_;
			});

		bus_.subscribe<events::ActionValue>(
			[this](const events::ActionValue& e)
			{
				if (debugLogging_)
					logActionValue(e);
			});
	}

	/*!
		@brief
			Adopts a newly created window, reads its focus and installs the per-window GLFW callbacks.
		@param e
			The window-created event
		*/
	void System::onWindowCreation(const window::events::WindowCreated& e)
	{
		if (!e.newWindow)
			return;

		// The system tracks exactly one window: window_ is a single pointer
		// and GLFW allows one user pointer per window. A second window would
		// silently steal the callbacks from the first.
		assert(!window_ && "input::System is single-window");

		window_ = e.newWindow;

		// The focus callback only reports changes, and a window can open
		// behind another application.
		focused_ = glfwGetWindowAttrib(e.newWindow, GLFW_FOCUSED) == GLFW_TRUE;

		glfwSetWindowUserPointer(e.newWindow, this);
		glfwSetKeyCallback(e.newWindow, KeyCallback);
		glfwSetCharCallback(e.newWindow, CharCallback);
		glfwSetCursorPosCallback(e.newWindow, CursorPosCallback);
		glfwSetMouseButtonCallback(e.newWindow, MouseButtonCallback);
		glfwSetScrollCallback(e.newWindow, ScrollCallback);
		glfwSetWindowFocusCallback(e.newWindow, FocusCallback);
	}

	/*!
		@brief
			Forgets the tracked window when it is the one destroyed.
		@param e
			The window-destroyed event
		*/
	void System::onWindowDestruction(const window::events::WindowDestroyed& e)
	{
		if (e.oldWindow != window_)
			return;

		// Drop it while it is merely stale. Holding on would leave
		// setCursorMode calling glfwSetInputMode on a destroyed window.
		window_ = nullptr;
	}

	/*!
		@brief
			Tops up GLFW's gamepad mappings from Assets/input/gamecontrollerdb.txt when the file exists.
		*/
	void System::loadGamepadMappings()
	{
		// GLFW 3.4 compiles an SDL_GameControllerDB snapshot in, so this is a
		// top-up for pads newer than the vendored GLFW, not a requirement.
		// Absence is the normal case.
		const std::string db =
			util::file_io::readFileIfExists("Assets/input/gamecontrollerdb.txt");

		if (db.empty())
			return;

		if (!glfwUpdateGamepadMappings(db.c_str()))
			std::cerr << "[input] gamecontrollerdb.txt present but rejected by GLFW\n";
	}

	/*!
		@brief
			Marks pads plugged in before startup as connected, then elects the primary pad.
		*/
	void System::enumerateGamepads()
	{
		// The joystick callback only fires on CHANGE, so a pad that was
		// plugged in before the process started never produces one.
		for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
		{
			if (glfwJoystickPresent(jid))
				pads_[static_cast<std::size_t>(jid)].onConnected();
		}

		updatePrimaryPad();
	}

	// ==================== GLFW callbacks ====================
	//
	// Every one of these ONLY enqueues. See raw_event.hpp for why.

	/*!
		@brief
			GLFW key callback; enqueues a Key raw event.
		@param win
			Window the event came from
		@param key
			GLFW key code
		@param scancode
			Platform scancode
		@param action
			GLFW_PRESS, GLFW_RELEASE or GLFW_REPEAT
		@param mods
			Modifier bitmask
		*/
	void System::KeyCallback(GLFWwindow* win, int key, int scancode, int action, int mods)
	{
		System* sys = fromWindow(win);
		if (!sys)
			return;

		RawEvent e;
		e.type_ = RawType::Key;
		e.code_ = key;
		e.scan_ = scancode;
		e.state_ = action;
		e.mods_ = mods;

		sys->queue_.push_back(e);
	}

	/*!
		@brief
			GLFW char callback; enqueues a Char raw event.
		@param win
			Window the event came from
		@param codepoint
			Unicode codepoint
		*/
	void System::CharCallback(GLFWwindow* win, unsigned int codepoint)
	{
		System* sys = fromWindow(win);
		if (!sys)
			return;

		RawEvent e;
		e.type_ = RawType::Char;
		e.x_ = static_cast<double>(codepoint);

		sys->queue_.push_back(e);
	}

	/*!
		@brief
			GLFW cursor position callback; enqueues a CursorPos raw event.
		@param win
			Window the event came from
		@param x
			Cursor X in window pixels
		@param y
			Cursor Y in window pixels
		*/
	void System::CursorPosCallback(GLFWwindow* win, double x, double y)
	{
		System* sys = fromWindow(win);
		if (!sys)
			return;

		RawEvent e;
		e.type_ = RawType::CursorPos;
		e.x_ = x;
		e.y_ = y;

		sys->queue_.push_back(e);
	}

	/*!
		@brief
			GLFW mouse button callback; enqueues a MouseButton raw event.
		@param win
			Window the event came from
		@param button
			GLFW mouse button
		@param action
			GLFW_PRESS or GLFW_RELEASE
		@param mods
			Modifier bitmask
		*/
	void System::MouseButtonCallback(GLFWwindow* win, int button, int action, int mods)
	{
		System* sys = fromWindow(win);
		if (!sys)
			return;

		RawEvent e;
		e.type_ = RawType::MouseButton;
		e.code_ = button;
		e.state_ = action;
		e.mods_ = mods;

		sys->queue_.push_back(e);
	}

	/*!
		@brief
			GLFW scroll callback; enqueues a Scroll raw event.
		@param win
			Window the event came from
		@param xOffset
			Horizontal wheel ticks
		@param yOffset
			Vertical wheel ticks
		*/
	void System::ScrollCallback(GLFWwindow* win, double xOffset, double yOffset)
	{
		System* sys = fromWindow(win);
		if (!sys)
			return;

		RawEvent e;
		e.type_ = RawType::Scroll;
		e.x_ = xOffset;
		e.y_ = yOffset;

		sys->queue_.push_back(e);
	}

	/*!
		@brief
			GLFW focus callback; enqueues a Focus raw event.
		@param win
			Window the event came from
		@param focused
			GLFW_TRUE when the window gained focus
		*/
	void System::FocusCallback(GLFWwindow* win, int focused)
	{
		System* sys = fromWindow(win);
		if (!sys)
			return;

		RawEvent e;
		e.type_ = RawType::Focus;
		e.state_ = (focused == GLFW_TRUE) ? 1 : 0;

		sys->queue_.push_back(e);
	}

	/*!
		@brief
			Enqueues a PadConnect raw event for a joystick connect or disconnect.
		@param jid
			Joystick slot
		@param event
			GLFW_CONNECTED or GLFW_DISCONNECTED
		*/
	void System::onJoystickEvent(int jid, int event)
	{
		RawEvent e;
		e.type_ = RawType::PadConnect;
		e.device_ = jid;
		e.state_ = (event == GLFW_CONNECTED) ? 1 : 0;

		queue_.push_back(e);
	}

	// ======================== Frame ========================

	/*!
		@brief
			Advances input by one frame: FrameStarted, drain, pad poll, drain, then action resolution and events.
		*/
	void System::update()
	{
		VOX_PROFILE_SCOPE_C("Input update", profile::colour::kInput);

		// First, so a consumer can zero its per-frame accumulators before
		// any of this frame's values arrive.
		bus_.publish(events::FrameStarted{});

		beginFrame();

		// Two passes: the first handles whatever the GLFW callbacks enqueued
		// during glfwPollEvents, including pad connects, so that the poll
		// below sees an accurate connection state.
		drainQueue();

		pollGamepads();
		drainQueue();

		resolveActions();
		publishActionEvents();
	}

	/*!
		@brief
			Clears every device's per-frame edges and accumulators at the top of the frame.
		*/
	void System::beginFrame()
	{
		// Cleared at the TOP of the frame, immediately before the drain,
		// rather than at the bottom of the previous one. Every reader in a
		// frame then sees exactly the same view, instead of state being live
		// across an ill-defined window spanning the frame boundary.
		keyboard_.beginFrame();
		mouse_.beginFrame();

		for (auto& pad : pads_)
			pad.beginFrame();
	}

	/*!
		@brief
			Swaps out the raw queue and handles every event in it, keeping the larger capacity.
		*/
	void System::drainQueue()
	{
		// Swap out rather than iterate in place: a handler reached through the
		// bus could conceivably cause more input to be enqueued, and the
		// vector must not resize underneath the loop.
		std::vector<RawEvent> batch;
		batch.swap(queue_);

		for (const RawEvent& e : batch)
			handleRawEvent(e);

		// Keep the capacity we paid for.
		if (queue_.capacity() < batch.capacity())
		{
			batch.clear();
			queue_.swap(batch);
		}
	}

	/*!
		@brief
			Applies one raw event to its device and publishes the matching device event, honouring suppression and rebinds.
		@param e
			The raw event
		*/
	void System::handleRawEvent(const RawEvent& e)
	{
		switch (e.type_)
		{
		case RawType::Key:
		{
			keyboard_.onKey(e.code_, e.state_, e.mods_);

			// The device still tracks the key, but a press typed into the
			// UI is not gameplay's to see. Releases always go out.
			if (keyboardSuppressed_ && e.state_ != GLFW_RELEASE)
				break;

			if (e.state_ == GLFW_PRESS)
			{
				if (tryCaptureRebind(action::Device::Keyboard, e.code_))
					break;

				bus_.publish(events::KeyPressed{ e.code_, e.scan_, e.mods_, false });
			}
			else if (e.state_ == GLFW_REPEAT)
			{
				bus_.publish(events::KeyPressed{ e.code_, e.scan_, e.mods_, true });
			}
			else if (e.state_ == GLFW_RELEASE)
			{
				bus_.publish(events::KeyReleased{ e.code_, e.scan_, e.mods_ });
			}

			break;
		}

		case RawType::Char:
			if (keyboardSuppressed_)
				break;

			bus_.publish(events::TextEntered{ static_cast<unsigned int>(e.x_) });
			break;

		case RawType::MouseButton:
		{
			mouse_.onButton(e.code_, e.state_, e.mods_);

			if (mouseSuppressed_ && e.state_ != GLFW_RELEASE)
				break;

			if (e.state_ == GLFW_PRESS)
			{
				if (tryCaptureRebind(action::Device::Mouse, e.code_))
					break;

				bus_.publish(events::MouseButtonPressed{
					e.code_, e.mods_, mouse_.position() });
			}
			else if (e.state_ == GLFW_RELEASE)
			{
				bus_.publish(events::MouseButtonReleased{
					e.code_, e.mods_, mouse_.position() });
			}

			break;
		}

		case RawType::CursorPos:
		{
			const glm::vec2 before = mouse_.delta();

			mouse_.onCursorPos(e.x_, e.y_);

			// The per-sample movement, not the frame accumulation: a
			// subscriber tracking a drag wants each step.
			bus_.publish(events::MouseMoved{ mouse_.position(),
											 mouse_.delta() - before });
			break;
		}

		case RawType::Scroll:
		{
			mouse_.onScroll(e.x_, e.y_);

			if (mouseSuppressed_)
				break;

			bus_.publish(events::MouseScrolled{ glm::vec2{
				static_cast<float>(e.x_), static_cast<float>(e.y_) } });
			break;
		}

		case RawType::Focus:
		{
			const bool focused = (e.state_ != 0);

			if (focused)
			{
				// The pointer moved while we were in another application, so
				// the next sample would otherwise arrive as one huge delta.
				mouse_.armNextSample();
			}
			else
			{
				// Do not leave anything stuck down across an alt-tab.
				keyboard_.onFocusLost();
				mouse_.onFocusLost();

				// Released, NOT disconnected: GLFW never re-announces a pad
				// that stayed plugged in, so a disconnect here left every pad
				// dead until it was physically replugged.
				for (auto& pad : pads_)
					pad.onFocusLost();
			}

			focused_ = focused;

			bus_.publish(events::WindowFocusChanged{ focused });
			break;
		}

		case RawType::PadButton:
		{
			if (e.device_ < 0 || static_cast<std::size_t>(e.device_) >= pads_.size())
				break;

			const bool isDown = (e.state_ != 0);

			device::Gamepad& pad = pads_[static_cast<std::size_t>(e.device_)];
			const bool wasDown = pad.down(e.code_);

			pad.applyButton(e.code_, isDown);

			if (isDown)
			{
				if (tryCaptureRebind(action::Device::Gamepad, e.code_))
					break;

				bus_.publish(events::GamepadButtonPressed{ e.device_, e.code_ });
			}
			else if (wasDown)
			{
				// A button held through a focus loss was dropped without an
				// edge, so its physical release has no press to pair with.
				bus_.publish(events::GamepadButtonReleased{ e.device_, e.code_ });
			}

			break;
		}

		case RawType::PadConnect:
		{
			if (e.device_ < 0 || static_cast<std::size_t>(e.device_) >= pads_.size())
				break;

			device::Gamepad& pad = pads_[static_cast<std::size_t>(e.device_)];

			if (e.state_ != 0)
			{
				pad.onConnected();

				bus_.publish(events::GamepadConnected{
					e.device_, pad.name(), pad.isGamepad() });
			}
			else
			{
				pad.onDisconnected();

				bus_.publish(events::GamepadDisconnected{ e.device_ });
			}

			updatePrimaryPad();
			break;
		}
		}
	}

	/*!
		@brief
			Polls every pad into the raw queue while focused, then re-elects the primary pad.
		*/
	void System::pollGamepads()
	{
		// Pads keep reporting while another application has focus. Keyboard
		// and mouse go quiet then, so pads do too.
		if (!focused_)
			return;

		for (auto& pad : pads_)
			pad.poll(queue_);

		// A pad can drop out inside poll() when GLFW refuses to hand over a
		// gamepad state, so re-elect before anything reads the primary.
		updatePrimaryPad();
	}

	/*!
		@brief
			Elects the lowest connected mapped pad as primary and publishes PrimaryGamepadChanged on a change.
		*/
	void System::updatePrimaryPad()
	{
		const int previous = primaryPad_;

		primaryPad_ = -1;

		for (int jid = 0; jid < static_cast<int>(keys::kMaxGamepads); ++jid)
		{
			const device::Gamepad& pad = pads_[static_cast<std::size_t>(jid)];

			// Lowest connected slot that GLFW can actually map. A present but
			// unmapped joystick - a flight stick, a dance mat - is skipped
			// rather than made primary and then producing nothing at all.
			if (pad.connected() && pad.isGamepad())
			{
				primaryPad_ = jid;
				break;
			}
		}

		if (primaryPad_ != previous)
			bus_.publish(events::PrimaryGamepadChanged{ previous, primaryPad_ });
	}

	// ======================= Actions =======================

	/*!
		@brief
			Evaluates every binding from the top of the context stack down into this frame's action states and edges.
		*/
	void System::resolveActions()
	{
		for (std::size_t i = 0; i < states_.size(); ++i)
		{
			prevDown_[i] = states_[i].down_;

			states_[i].clearFrame();
			states_[i].lastDevice_ = action::Device::None;
		}

		consumed_.clear();

		action::DeviceView view;
		// A device the UI owns is simply absent: every evaluation treats a
		// null device as "not actuated", so whatever it held is released.
		view.keyboard_ = keyboardSuppressed_ ? nullptr : &keyboard_;
		view.mouse_ = mouseSuppressed_ ? nullptr : &mouse_;
		view.pad_ = (primaryPad_ >= 0)
			? &pads_[static_cast<std::size_t>(primaryPad_)]
			: nullptr;

		// Highest priority is the top of the stack, so walk it backwards.
		for (auto it = contextStack_.rbegin(); it != contextStack_.rend(); ++it)
		{
			action::ActionMap& map = maps_[static_cast<std::size_t>(*it)];

			if (!map.enabled())
				continue;

			for (const action::Binding& b : map.bindings())
			{
				if (!action::isAvailable(b, consumed_))
					continue;

				const action::Candidate cand = action::evaluate(b, view);

				action::ActionState& st = states_[b.action_];
				st.context_ = *it;

				action::mergeCandidate(st, cand);
			}

			// Claimed after the whole map is evaluated, so bindings inside one
			// map never block each other, and claimed on BIND rather than on
			// fire, so an open menu swallows W even on the frames it has no
			// use for it.
			if (map.blocking())
			{
				for (const action::Binding& b : map.bindings())
					action::consumeSources(b, consumed_);
			}
		}

		for (std::size_t i = 0; i < states_.size(); ++i)
		{
			states_[i].pressed_ = states_[i].down_ && !prevDown_[i];
			states_[i].released_ = !states_[i].down_ && prevDown_[i];
		}
	}

	/*!
		@brief
			Publishes Started / Performed / Canceled edges, then ActionValue for every actuated analog action.
		*/
	void System::publishActionEvents()
	{
		for (std::size_t i = 0; i < states_.size(); ++i)
		{
			const action::ActionState& st = states_[i];
			const auto a = static_cast<action::Action>(i);

			if (a == action::None)
				continue;

			if (st.pressed_)
			{
				bus_.publish(events::ActionStarted{ a, st.context_, st.lastDevice_ });

				// Started and Performed land on the same frame while there
				// are no hold or tap interactions. Keeping both in the event
				// set means adding them later changes only WHEN Performed
				// fires, not what any subscriber listens for.
				if (action::kValueType[i] == action::ValueType::Button)
				{
					bus_.publish(events::ActionPerformed{
						a, st.context_, glm::length(st.value_) });
				}
			}

			if (st.released_)
				bus_.publish(events::ActionCanceled{ a, st.context_ });
		}

		// Values after every edge, so a handler reacting to ActionStarted has
		// already run when the first value of that actuation arrives.
		// down_ on an analog action means actuated, so an action that sends
		// nothing this frame is at zero.
		for (std::size_t i = 0; i < states_.size(); ++i)
		{
			const action::ActionState& st = states_[i];

			if (!st.down_ || action::kValueType[i] == action::ValueType::Button)
				continue;

			bus_.publish(events::ActionValue{
				static_cast<action::Action>(i), st.context_, st.value_, st.lastDevice_ });
		}
	}

	// ======================= Contexts =======================

	/*!
		@brief
			Pushes an action map to the top of the priority stack unless already present.
		@param c
			Context to activate
		*/
	void System::pushContext(action::Context c)
	{
		for (action::Context existing : contextStack_)
		{
			if (existing == c)
				return;
		}

		contextStack_.push_back(c);
	}

	/*!
		@brief
			Removes an action map from the priority stack.
		@param c
			Context to deactivate
		*/
	void System::popContext(action::Context c)
	{
		for (auto it = contextStack_.begin(); it != contextStack_.end(); ++it)
		{
			if (*it == c)
			{
				contextStack_.erase(it);
				return;
			}
		}
	}

	/*!
		@brief
			Enables or disables a map without changing its stack position.
		@param c
			Context to affect
		@param enabled
			false to make every binding in it inert
		*/
	void System::setContextEnabled(action::Context c, bool enabled)
	{
		if (c < action::Context::Count)
			maps_[static_cast<std::size_t>(c)].setEnabled(enabled);
	}

	/*!
		@brief
			Makes a context swallow every source it binds.
		@param c
			Context to affect
		@param blocking
			true to hide those sources from lower-priority maps
		*/
	void System::setContextBlocking(action::Context c, bool blocking)
	{
		if (c < action::Context::Count)
			maps_[static_cast<std::size_t>(c)].setBlocking(blocking);
	}

	/*!
		@brief
			Records which devices a UI layer currently owns.
		@param keyboard
			true while the UI is taking keyboard input
		@param mouse
			true while the UI is taking mouse input
		*/
	void System::setSuppressed(bool keyboard, bool mouse)
	{
		keyboardSuppressed_ = keyboard;
		mouseSuppressed_ = mouse;
	}

	// ====================== Rebinding =======================

	/*!
		@brief
			Adds one source to an action in a context.
		@param c
			Context to bind in
		@param b
			Binding to add
		*/
	void System::addBinding(action::Context c, const action::Binding& b)
	{
		if (c < action::Context::Count)
			maps_[static_cast<std::size_t>(c)].addBinding(b);
	}

	/*!
		@brief
			Removes every source driving an action in one context.
		@param c
			Context to clear in
		@param a
			Action to unbind
		*/
	void System::clearBindings(action::Context c, action::Action a)
	{
		if (c < action::Context::Count)
			maps_[static_cast<std::size_t>(c)].clearBindings(a);
	}

	/*!
		@brief
			Clears every map and reloads the compile-time default bindings into each action's default context.
		*/
	void System::resetBindingsToDefaults()
	{
		for (auto& map : maps_)
			map.clear();

		for (const action::Binding& b : action::defaultBindings())
		{
			const action::Context c = action::kDefaultContext[b.action_];

			maps_[static_cast<std::size_t>(c)].addBinding(b);
		}
	}

	/*!
		@brief
			Arms interactive rebinding for an action in a context; invalid ids are ignored.
		@param c
			Context to rebind in
		@param a
			Action to rebind
		*/
	void System::startInteractiveRebind(action::Context c, action::Action a)
	{
		if (a >= action::Count || c >= action::Context::Count)
			return;

		rebinding_ = true;
		rebindContext_ = c;
		rebindAction_ = a;
	}

	/*!
		@brief
			Disarms an armed rebind and publishes a canceled RebindCompleted.
		*/
	void System::cancelInteractiveRebind()
	{
		if (!rebinding_)
			return;

		const action::Action a = rebindAction_;

		rebinding_ = false;
		rebindAction_ = action::None;

		bus_.publish(events::RebindCompleted{ a, true });
	}

	/*!
		@brief
			Feeds a discrete press to an armed rebind: Escape cancels, anything else replaces the action's bindings.
		@param device
			Device the press came from
		@param code
			Button or key code
		@return
			true when the press was swallowed by the rebind
		*/
	bool System::tryCaptureRebind(action::Device device, int code)
	{
		if (!rebinding_)
			return false;

		// Escape always cancels, so a rebind can never leave the player
		// unable to back out of the options screen.
		if (device == action::Device::Keyboard && code == keys::KeyCode::Escape)
		{
			cancelInteractiveRebind();
			return true;
		}

		const action::Action a = rebindAction_;
		const action::Context c = rebindContext_;

		rebinding_ = false;
		rebindAction_ = action::None;

		maps_[static_cast<std::size_t>(c)].clearBindings(a);
		maps_[static_cast<std::size_t>(c)].addBinding(action::btn(a, device, code));

		bus_.publish(events::RebindCompleted{ a, false });

		return true;
	}

	// ====================== Gamepad =========================

	/*!
		@brief
			Whether a slot holds a connected, mapped gamepad.
		@param slot
			Joystick slot
		@return
			true when it can produce state
		*/
	bool System::padConnected(int slot) const
	{
		if (slot < 0 || static_cast<std::size_t>(slot) >= pads_.size())
			return false;

		const device::Gamepad& pad = pads_[static_cast<std::size_t>(slot)];

		return pad.connected() && pad.isGamepad();
	}

	/*!
		@brief
			Product name of the pad in a slot.
		@param slot
			Joystick slot
		@return
			Name, or an empty string for an out-of-range or empty slot
		*/
	std::string System::padName(int slot) const
	{
		if (slot < 0 || static_cast<std::size_t>(slot) >= pads_.size())
			return {};

		return pads_[static_cast<std::size_t>(slot)].name();
	}

	// ======================= Cursor =========================

	/*!
		@brief
			Sets the GLFW cursor mode (raw motion when disabled), re-arms the mouse sample and publishes CursorModeChanged.
		@param mode
			GLFW_CURSOR_NORMAL, GLFW_CURSOR_HIDDEN or GLFW_CURSOR_DISABLED
		*/
	void System::setCursorMode(int mode)
	{
		if (!window_)
			return;

		cursorMode_ = mode;
		glfwSetInputMode(window_, GLFW_CURSOR, mode);

		// Raw motion strips the OS pointer acceleration curve out of the
		// look, which is what makes mouse-look feel like a game rather than
		// like dragging a desktop cursor.
		if (mode == GLFW_CURSOR_DISABLED && glfwRawMouseMotionSupported())
			glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
		else
			glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);

		// Capturing teleports the pointer to the window centre, which would
		// otherwise surface as one huge bogus delta.
		mouse_.armNextSample();

		bus_.publish(events::CursorModeChanged{ mode, cursorCaptured() });
	}

	/*!
		@brief
			Captures (disabled cursor) or releases (normal cursor) the cursor.
		@param captured
			true to hide and lock the cursor to the window
		*/
	void System::setCursorCaptured(bool captured)
	{
		setCursorMode(captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	}

	/*!
		@brief
			Whether the cursor is captured for mouse-look.
		@return
			true when the cursor mode is GLFW_CURSOR_DISABLED
		*/
	bool System::cursorCaptured() const
	{
		return cursorMode_ == GLFW_CURSOR_DISABLED;
	}

	// ======================== Debug =========================

	/*!
		@brief
			Prints one ActionValue (its components, device and primary pad) to stdout.
		@param e
			The value event to print
		*/
	void System::logActionValue(const events::ActionValue& e) const
	{
		static constexpr const char* kDeviceName[] = { "keyboard", "mouse", "pad", "none" };

		const int n = action::componentCount(e.action_);

		std::cout << "[input] " << action::name(e.action_) << "=(" << e.value_.x;

		if (n > 1)
			std::cout << ", " << e.value_.y;

		if (n > 2)
			std::cout << ", " << e.value_.z;

		// A trigger-bound ZoomRate showing up here with nothing pulled is the
		// rest-at-minus-one trap - see device::triggerToUnit.
		std::cout << ") " << kDeviceName[static_cast<std::size_t>(e.device_)]
			<< " pad=" << primaryPad_ << "\n";
	}
}
