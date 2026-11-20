/*************************************************************************************************************
\file       lua_api_audio.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Lua Audio and AudioChannel tables: play / pause / stop sources by scene object ID, and pause or
            set the volume of a mixer channel, all through audio bus events.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "lua_api.hpp"

// Engine
#include "../interface/lua_interface.hpp"
#include "../../events/bus.hpp"
#include "../../audio/sys/audio_sys.hpp"
#include "scene/SceneManager.hpp"

// stds
#include <cstdint>


namespace scripting::lua
{
	/*!
		@brief
			Registers Audio.Play/Pause/Stop, PauseChannel, SetChannelVolume and the AudioChannel table.
		@param lua
			Lua interface to register into
		@param bus
			Event bus the audio commands are published on
		@param scenes
			Scene manager that maps stable object IDs to entities
		*/
	void registerAudioApi(Interface& lua, ::events::Bus& bus, vox::SceneManager& scenes)
	{
		// Audio APIs use the stable Object ID displayed by the scene inspector.
		lua.link("Audio", "Play", [&bus, &scenes](uint32_t id) {
			bus.publish(audio::SourceCommand{scenes.entity(id), audio::Action::Play});
		});
		lua.link("Audio", "Pause", [&bus, &scenes](uint32_t id) {
			bus.publish(audio::SourceCommand{scenes.entity(id), audio::Action::Pause});
		});
		lua.link("Audio", "Stop", [&bus, &scenes](uint32_t id) {
			bus.publish(audio::SourceCommand{scenes.entity(id), audio::Action::Stop});
		});
		auto channels = lua.createTable("AudioChannel");
		channels["Music"] = static_cast<int>(cpn::AudioChannel::Music);
		channels["SFX"] = static_cast<int>(cpn::AudioChannel::SFX);
		channels["UI"] = static_cast<int>(cpn::AudioChannel::UI);
		lua.link("Audio", "PauseChannel", [&bus](int channel, bool paused) {
			bus.publish(audio::ChannelPause{static_cast<cpn::AudioChannel>(channel), paused});
		});
		lua.link("Audio", "SetChannelVolume", [&bus](int channel, float volume) {
			bus.publish(audio::ChannelVolume{static_cast<cpn::AudioChannel>(channel), volume});
		});
	}
}
