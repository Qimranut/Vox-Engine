#pragma once
#include "scheduler/sys_interface.hpp"
#include "audio/audio_events.hpp"
#include "resources/ResourceManager.hpp"
#include "events/bus.hpp"
#include <array>
#include <unordered_set>

namespace FMOD { class System; class Channel; class ChannelGroup; class Sound; }
namespace audio {
// Main-thread service: ECS, resources, Lua and editor events share the same owner.
class System : public scheduler::ISystem {
public:
    ~System();
    void init(events::Bus&, entt::registry&, vox::ResourceManager&);
    void update(double) override;
    const char* getName() const override { return "Audio"; }
    void shutdown();
private:
    struct Clip {
        vox::ResourceHandle resource;
        FMOD::Sound* sound{};
        ~Clip();
    };
    struct Voice { std::shared_ptr<Clip> clip; FMOD::Channel* channel{}; std::string path; };
    void command(const SourceCommand&);
    void removed(entt::registry&, entt::entity);
    void stop(entt::entity);
    void configure(Voice&, const cpn::AudioSource&);
    FMOD::ChannelGroup* group(cpn::AudioChannel) const;
    FMOD::System* system_{};
    FMOD::ChannelGroup* master_{};
    std::array<FMOD::ChannelGroup*, 3> groups_{};
    events::Bus* bus_{};
    entt::registry* registry_{};
    vox::ResourceManager* resources_{};
    std::vector<events::SubscriptionId> subscriptions_;
    std::unordered_map<entt::entity, Voice> voices_;
    std::unordered_map<const vox::Resource*, std::weak_ptr<Clip>> clips_;
    std::unordered_set<entt::entity> awakened_;
    bool paused_{true};
};
}
