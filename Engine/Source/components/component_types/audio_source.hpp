#pragma once
#include <string>

namespace cpn {
enum class AudioChannel { Music, SFX, UI };
// Persistent settings only. FMOD handles belong to the audio system.
struct AudioSource {
    std::string path;
    AudioChannel channel{AudioChannel::SFX};
    float volume{1.0f};
    bool loop{}, playOnAwake{};
};
}
