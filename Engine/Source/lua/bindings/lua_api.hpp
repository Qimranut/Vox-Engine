/*************************************************************************************************************
\file       lua_api.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Every C++ -> Lua binding the engine exposes, one register function per Lua table. Each takes
            only the subsystems its table needs; lambdas capture those by reference, and SystemManager
            owns them on the heap, so the addresses stay valid for the life of the Lua state.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace events	{ class Bus; }
namespace input		{ class System; }
namespace graphics	{ class System; }
namespace physics	{ class System; }
namespace editor	{ class System; }
namespace voxel		{ class System; }
namespace vox		{ class SceneManager; }
namespace engine	{ class SystemManager; }


namespace scripting::lua
{
	class Interface;
	class InputEventBridge;

	/*!
		@brief
			Registers every table below.
		@param systems
			Constructed subsystems with the Lua interface initialised
		*/
	void registerEngineApi(engine::SystemManager& systems);

	/*!
		@brief
			Registers Audio.Play/Pause/Stop by scene object ID, PauseChannel,
			SetChannelVolume, and the AudioChannel table.
		@param lua
			Lua interface to register into
		@param bus
			Event bus the audio commands are published on
		@param scenes
			Scene manager that maps object IDs to entities
		*/
	void registerAudioApi(Interface& lua, ::events::Bus& bus, vox::SceneManager& scenes);

	/*!
		@brief
			Registers the Key, Mouse, Action, Pad and PadAxis tables, and the Input table:
			callbacks, gamepads, rebinding and contexts.
		@param lua
			Lua interface to register into
		@param bridge
			Owner of the Lua input subscriptions
		@param inputSys
			The input system
		*/
	void registerInputApi(Interface& lua, InputEventBridge& bridge, input::System& inputSys);

	/*!
		@brief
			Registers the Camera table. Angles cross in degrees.
		@param lua
			Lua interface to register into
		@param gph
			Graphics system that owns the camera
		*/
	void registerCameraApi(Interface& lua, graphics::System& gph);

	/*!
		@brief
			Registers the Physics table. Entities cross as integers.
		@param lua
			Lua interface to register into
		@param bridge
			Owner of the Lua contact and trigger subscriptions
		@param phys
			The physics system
		*/
	void registerPhysicsApi(Interface& lua, InputEventBridge& bridge, physics::System& phys);

	/*!
		@brief
			Registers the Node table.
		@param lua
			Lua interface to register into
		@param editorSys
			Engine-side scene tree system
		*/
	void registerNodeApi(Interface& lua, editor::System& editorSys);

	/*!
		@brief
			Registers the Voxel table: material queries and edits on the loaded voxel
			world. Coordinates and ids cross as integers.
		@param lua
			Lua interface to register into
		@param vox
			The voxel system
		*/
	void registerVoxelApi(Interface& lua, voxel::System& vox);
}
