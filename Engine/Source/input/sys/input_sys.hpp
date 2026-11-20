/*************************************************************************************************************
\file       input_sys.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      input::System: GLFW backend that only enqueues raw events, the keyboard / mouse / gamepad
            devices, and the context-stacked action layer, all published to consumers through the bus.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Window events
#include "../../window/events/win_events.hpp"

// Event bus
#include "../../events/bus.hpp"

// Input events
#include "../events/raw_event.hpp"
#include "../events/input_events.hpp"

// Devices
#include "../device/mouse.hpp"
#include "../device/gamepad.hpp"
#include "../device/keyboard.hpp"

// Actions
#include "../action/action_map.hpp"
#include "../action/action_state.hpp"

// Definitions
#include "../definitions/input_def.hpp"
#include "../definitions/action_list.hpp"

// stds
#include <array>
#include <vector>
#include <string>


// Deliberately forward declared rather than including GLFW. This header is
// pulled in by the engine's glue (SystemManager, wiring, Lua API), and every GLFW symbol it exposes is one more thing
// the rest of the engine can reach for by accident.
struct GLFWwindow;


namespace input
{
	/**
	 * The input system.
	 *
	 * Three layers, bottom up:
	 *
	 *  1. GLFW backend - callbacks that only ENQUEUE a RawEvent, plus a
	 *     per-frame poll of the gamepads, which have no callbacks.
	 *  2. Devices - Keyboard, Mouse and up to sixteen Gamepads, each holding
	 *     held/pressed/released state and this frame's accumulated deltas.
	 *  3. Actions - named controls bound to sources across every device, with
	 *     stacked contexts so a menu can swallow input the camera would
	 *     otherwise read.
	 *
	 * Everything reaches consumers through events::Bus - see
	 * input_events.hpp for the event set and the per-frame order. There are
	 * deliberately no down/pressed/value queries: a consumer subscribes to
	 * the action events, and analog values arrive as ActionValue once per
	 * frame. Consumers should subscribe to ACTION events rather than raw key
	 * events, or they hardcode a keybinding and give up gamepad support.
	 *
	 * The few queries left are status, not input: pad connection, cursor
	 * mode, rebind state. The bus has no replay, so a subscriber that arrives
	 * late needs some way to learn the state it missed.
	 *
	 * Main thread only, and deliberately NOT a scheduler::ISystem: every GLFW
	 * input call it makes is documented main-thread-only, and the scheduler
	 * runs last in the frame, which would resolve actions after everyone had
	 * already consumed them.
	 *
	 */
	class System
	{
	public:

		/*!
			@brief
				Registers as the joystick-callback owner, sets up the action maps and pads, and loads the default bindings.

				Fully qualified: input::events exists, so an unqualified events::Bus
				resolves to it and not to the global bus namespace.
			@param bus
				Event bus every input event is published on
			*/
		explicit System(::events::Bus& bus);
		/*!
			@brief
				Removes the global joystick callback and releases the owner slot.
			*/
		~System();

		System(const System&) = delete;
		System& operator=(const System&) = delete;

		/*!
			@brief
				Subscribes to the window events and to this system's own action
				events, and installs the GLFW callbacks that do not depend on a
				window.

				Must run BEFORE the window is created: the bus has no replay, so a
				subscriber registered after WindowCreated is published never learns
				the window exists and every callback registration silently never
				happens.
			*/
		void subscriptionSetup();

		/*!
			@brief
				Advances input by one frame.

				Call once, immediately after glfwPollEvents and before anything
				that depends on this frame's input. Publishes FrameStarted, clears
				the previous frame's edges, polls the gamepads, drains the raw
				queue into the devices publishing device events, then resolves
				every action and publishes the action events.

				Takes no delta time on purpose. Nothing here is rate-dependent -
				the one dt-sensitive conversion, turning a stick rate into an
				angle, belongs to whichever consumer knows which dt it is using.
			*/
		void update();

		// =================== Contexts ===================

		/*!
			@brief
				Pushes an action map to the top of the priority stack.
			@param c
				Context to activate. Pushing one already on the stack is a no-op.
			*/
		void pushContext(action::Context c);

		/*!
			@brief
				Removes an action map from the priority stack.
			@param c
				Context to deactivate
			*/
		void popContext(action::Context c);

		/*!
			@brief
				Enables or disables a map without changing its stack position.
			@param c
				Context to affect
			@param enabled
				false to make every binding in it inert
			*/
		void setContextEnabled(action::Context c, bool enabled);

		/*!
			@brief
				Makes a context swallow every source it binds.
			@param c
				Context to affect
			@param blocking
				true to hide those sources from lower-priority maps
			*/
		void setContextBlocking(action::Context c, bool blocking);

		/*!
			@brief
				Hides a device from gameplay while a UI layer owns it.

				A suppressed device drives no action, so anything it was holding
				is canceled on the next update, and its press, text and scroll
				events are not published. Releases and cursor movement still
				are, so held-state trackers never stick.
			@param keyboard
				true while the UI is taking keyboard input
			@param mouse
				true while the UI is taking mouse input
			*/
		void setSuppressed(bool keyboard, bool mouse);

		// =================== Rebinding ===================

		/*!
			@brief
				Adds one source to an action without disturbing the others.
			@param c
				Context to bind in
			@param b
				Binding to add
			*/
		void addBinding(action::Context c, const action::Binding& b);

		/*!
			@brief
				Removes every source driving an action in one context.
			@param c
				Context to clear in
			@param a
				Action to unbind
			*/
		void clearBindings(action::Context c, action::Action a);

		/*!
			@brief
				Restores the compile-time default binding table.
			*/
		void resetBindingsToDefaults();

		/*!
			@brief
				Arms interactive rebinding.

				The next discrete button press from any device replaces this
				action's bindings in [c]. Escape cancels. The result arrives as
				input::events::RebindCompleted, published from the frame drain so
				a handler may immediately arm another.
			@param c
				Context to rebind in
			@param a
				Action to rebind
			*/
		void startInteractiveRebind(action::Context c, action::Action a);

		/*!
			@brief
				Disarms an armed interactive rebind and publishes a canceled RebindCompleted.
			*/
		void cancelInteractiveRebind();

		/*!
			@brief
				Whether an interactive rebind is armed.
			@return
				true while waiting for the rebind press
			*/
		bool rebindInProgress() const { return rebinding_; }

		/*!
			@brief
				Captures the cursor while an action is held by a MOUSE source.

				Deliberately mouse-only: capture exists to stop the OS pointer
				escaping the window during a look drag. A gamepad has no pointer,
				so a pad binding on the same action must not hide the user's
				cursor.
			@param a
				Action to watch, or Action::None to disable the policy
			*/
		void setCaptureWhileHeld(action::Action a) { captureWhileHeld_ = a; }

		// =================== Gamepad status ===================
		//
		// Status, not input: GamepadConnected / PrimaryGamepadChanged carry
		// the changes, but a pad plugged in before a subscriber existed never
		// produces one for it.

		/*!
			@brief
				Slot of the pad currently driving the game.
			@return
				Joystick slot, or -1 when no mapped pad is connected
			*/
		int primaryPad() const { return primaryPad_; }

		/*!
			@brief
				Whether a slot holds a connected, mapped gamepad.
			@param slot
				Joystick slot
			@return
				true when it can produce state
			*/
		bool padConnected(int slot) const;

		/*!
			@brief
				Product name of the pad in a slot.
			@param slot
				Joystick slot
			@return
				Name, or an empty string when the slot is empty
			*/
		std::string padName(int slot) const;

		// =================== Cursor ===================

		/*!
			@brief
				Sets the GLFW cursor mode on the window.
			@param mode
				GLFW_CURSOR_NORMAL, GLFW_CURSOR_HIDDEN or GLFW_CURSOR_DISABLED
			*/
		void setCursorMode(int mode);

		/*!
			@brief
				Captures or releases the cursor for mouse-look.
			@param captured
				true to hide and lock the cursor to the window
			*/
		void setCursorCaptured(bool captured);

		/*!
			@brief
				Current GLFW cursor mode.
			@return
				GLFW_CURSOR_NORMAL, GLFW_CURSOR_HIDDEN or GLFW_CURSOR_DISABLED
			*/
		int  cursorMode() const { return cursorMode_; }
		/*!
			@brief
				Whether the cursor is captured for mouse-look.
			@return
				true when the cursor mode is GLFW_CURSOR_DISABLED
			*/
		bool cursorCaptured() const;

		// =================== GLFW plumbing ===================

		/*!
			@brief
				Receives a joystick connect or disconnect.

				Public only because glfwSetJoystickCallback is a global callback
				with no user pointer, so a free function has to reach in. Enqueues
				and returns; it never touches GLFW or the bus.
			@param jid
				Joystick slot
			@param event
				GLFW_CONNECTED or GLFW_DISCONNECTED
			*/
		void onJoystickEvent(int jid, int event);

	private:

		::events::Bus&	bus_;

		device::Keyboard	keyboard_	{};
		device::Mouse		mouse_		{};

		std::array<device::Gamepad, keys::kMaxGamepads>	pads_	{};

		// Lowest connected mapped pad. Everything bound to Device::Gamepad
		// reads this one.
		int	primaryPad_	{ -1 };

		std::vector<RawEvent>	queue_;

		GLFWwindow*	window_		{ nullptr };
		int			cursorMode_	{ 0 };

		// Pads are not polled while another application has focus.
		bool		focused_	{ true };

		// ------------------------- Actions -------------------------

		std::array<action::ActionMap, static_cast<std::size_t>(action::Context::Count)> maps_;

		// Lowest priority first, so resolution walks it backwards.
		std::vector<action::Context>	contextStack_;

		std::array<action::ActionState, action::Count>	states_		{};
		std::array<bool, action::Count>					prevDown_	{};

		action::ConsumedSet	consumed_	{};

		action::Action	captureWhileHeld_	{ action::None };

		// Devices a UI layer currently owns; see setSuppressed.
		bool	keyboardSuppressed_	{ false };
		bool	mouseSuppressed_	{ false };

		// ------------------------ Rebinding ------------------------

		bool			rebinding_		{ false };
		action::Context	rebindContext_	{ action::Context::Gameplay };
		action::Action	rebindAction_	{ action::None };

		// ------------------------- Debug ---------------------------

		bool	debugLogging_	{ false };

		// ---------------------- GLFW callbacks ---------------------

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
		static void KeyCallback(GLFWwindow* win, int key, int scancode, int action, int mods);
		/*!
			@brief
				GLFW char callback; enqueues a Char raw event.
			@param win
				Window the event came from
			@param codepoint
				Unicode codepoint
			*/
		static void CharCallback(GLFWwindow* win, unsigned int codepoint);
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
		static void CursorPosCallback(GLFWwindow* win, double x, double y);
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
		static void MouseButtonCallback(GLFWwindow* win, int button, int action, int mods);
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
		static void ScrollCallback(GLFWwindow* win, double xOffset, double yOffset);
		/*!
			@brief
				GLFW focus callback; enqueues a Focus raw event.
			@param win
				Window the event came from
			@param focused
				GLFW_TRUE when the window gained focus
			*/
		static void FocusCallback(GLFWwindow* win, int focused);

		/*!
			@brief
				Adopts a newly created window and installs the per-window GLFW callbacks on it.
			@param e
				The window-created event
			*/
		void onWindowCreation(const window::events::WindowCreated& e);
		/*!
			@brief
				Forgets the tracked window when it is destroyed.
			@param e
				The window-destroyed event
			*/
		void onWindowDestruction(const window::events::WindowDestroyed& e);

		// ------------------------- Internals -----------------------

		/*!
			@brief
				Clears every device's per-frame edges and accumulators.
			*/
		void beginFrame();
		/*!
			@brief
				Handles every queued raw event in order.
			*/
		void drainQueue();
		/*!
			@brief
				Applies one raw event to its device and publishes the matching device event.
			@param e
				The raw event
			*/
		void handleRawEvent(const RawEvent& e);

		/*!
			@brief
				Tops up GLFW's gamepad mappings from Assets/input/gamecontrollerdb.txt when present.
			*/
		void loadGamepadMappings();
		/*!
			@brief
				Marks pads already plugged in at startup as connected.
			*/
		void enumerateGamepads();
		/*!
			@brief
				Polls every pad into the raw queue while the window has focus.
			*/
		void pollGamepads();
		/*!
			@brief
				Re-elects the lowest connected mapped pad and publishes a change.
			*/
		void updatePrimaryPad();

		/*!
			@brief
				Evaluates every binding across the context stack into this frame's action states.
			*/
		void resolveActions();
		/*!
			@brief
				Publishes the action edge events, then ActionValue for every actuated analog action.
			*/
		void publishActionEvents();

		/*!
			@brief
				Subscribes the system's own consumers - the cursor capture policy
				and the debug toggle and log - to its action events.
			*/
		void subscribeInternalConsumers();

		/*!
			@brief
				Feeds a discrete press to an armed interactive rebind.
			@param device
				Device the press came from
			@param code
				Button or key code
			@return
				true when the press was swallowed by the rebind
			*/
		bool tryCaptureRebind(action::Device device, int code);

		/*!
			@brief
				Prints one ActionValue to stdout for the DebugInput log.
			@param e
				The value event to print
			*/
		void logActionValue(const events::ActionValue& e) const;
	};
}
