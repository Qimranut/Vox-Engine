#pragma once
#include "components/component_types/audio_source.hpp"
#include <entt.hpp>

namespace audio {
// Publish on the engine thread, like the existing input events.
enum class Action { Play, Pause, Stop };
struct SourceCommand { entt::entity entity; Action action; };
struct ChannelPause { cpn::AudioChannel channel; bool paused; };
struct ChannelVolume { cpn::AudioChannel channel; float volume; };
struct SimulationPause { bool paused; };
struct SourceState { bool playing{}, paused{}; unsigned positionMs{}; };
// Synchronous read-only request; the caller owns the result.
struct QuerySource { entt::entity entity; SourceState& result; };
}
