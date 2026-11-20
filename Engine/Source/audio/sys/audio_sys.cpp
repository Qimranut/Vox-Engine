#include "audio_sys.hpp"
#include "events/bus.hpp"
#include "profile/profiler.hpp"
#include <fmod.hpp>
#include <fmod_errors.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace audio {
namespace {
void check(FMOD_RESULT result) {
    if (result != FMOD_OK) throw std::runtime_error(std::string("Audio: ") + FMOD_ErrorString(result));
}
void volume(float value) {
    if (!std::isfinite(value) || value < 0 || value > 1)
        throw std::runtime_error("Audio volume must be between 0 and 1.");
}
}
System::Clip::~Clip() { if (sound) sound->release(); }
System::~System() { shutdown(); }
FMOD::ChannelGroup* System::group(cpn::AudioChannel channel) const {
    const auto index = static_cast<size_t>(channel);
    if (index >= groups_.size()) throw std::runtime_error("Unknown audio channel.");
    return groups_[index];
}
void System::init(events::Bus& bus, entt::registry& registry, vox::ResourceManager& resources) {
    shutdown();
    bus_ = &bus; registry_ = &registry; resources_ = &resources;
    try {
        check(FMOD::System_Create(&system_));
        check(system_->init(512, FMOD_INIT_NORMAL, nullptr));
        check(system_->getMasterChannelGroup(&master_));
        const char* names[]{"Music", "SFX", "UI"};
        for (size_t i = 0; i < groups_.size(); ++i) {
            check(system_->createChannelGroup(names[i], &groups_[i]));
            check(master_->addGroup(groups_[i]));
        }
        paused_ = true;
        check(master_->setPaused(true));
        subscriptions_.reserve(5);
        subscriptions_.push_back(bus.subscribe<SourceCommand>([this](const auto& e) { command(e); }));
        subscriptions_.push_back(bus.subscribe<ChannelPause>([this](const auto& e) { check(group(e.channel)->setPaused(e.paused)); }));
        subscriptions_.push_back(bus.subscribe<ChannelVolume>([this](const auto& e) { volume(e.volume); check(group(e.channel)->setVolume(e.volume)); }));
        subscriptions_.push_back(bus.subscribe<SimulationPause>([this](const auto& e) {
            check(master_->setPaused(e.paused)); paused_ = e.paused;
        }));
        subscriptions_.push_back(bus.subscribe<QuerySource>([this](const auto& e) {
            e.result = {};
            const auto it = voices_.find(e.entity);
            if (it == voices_.end()) return;
            auto* channel = it->second.channel;
            bool playing{}, sourcePaused{}, groupPaused{};
            if (channel->isPlaying(&playing) != FMOD_OK || !playing) return;
            if (channel->getPaused(&sourcePaused) != FMOD_OK) return;
            FMOD::ChannelGroup* mixer{};
            if (channel->getChannelGroup(&mixer) == FMOD_OK && mixer) mixer->getPaused(&groupPaused);
            e.result.playing = true;
            e.result.paused = paused_ || sourcePaused || groupPaused;
            channel->getPosition(&e.result.positionMs, FMOD_TIMEUNIT_MS);
        }));
        registry.on_destroy<cpn::AudioSource>().connect<&System::removed>(*this);
    } catch (...) { shutdown(); throw; }
}
void System::configure(Voice& voice, const cpn::AudioSource& source) {
    volume(source.volume);
    check(voice.channel->setChannelGroup(group(source.channel)));
    check(voice.channel->setVolume(source.volume));
    check(voice.channel->setMode(source.loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF));
    check(voice.channel->setLoopCount(source.loop ? -1 : 0));
}
void System::stop(entt::entity entity) {
    if (auto it = voices_.find(entity); it != voices_.end()) {
        it->second.channel->stop();
        voices_.erase(it);
    }
}
void System::removed(entt::registry&, entt::entity entity) {
    stop(entity); awakened_.erase(entity);
}
void System::command(const SourceCommand& event) {
    if (!system_) throw std::runtime_error("Audio is not initialized.");
    if (!registry_->valid(event.entity) || !registry_->all_of<cpn::AudioSource>(event.entity))
        throw std::runtime_error("Object needs an Audio Source component.");
    const auto& source = registry_->get<cpn::AudioSource>(event.entity);
    awakened_.insert(event.entity);
    if (event.action == Action::Stop) { stop(event.entity); return; }
    if (auto it = voices_.find(event.entity); it != voices_.end()) {
        bool playing{};
        if (it->second.channel->isPlaying(&playing) == FMOD_OK && playing && it->second.path == source.path) {
            configure(it->second, source);
            check(it->second.channel->setPaused(event.action == Action::Pause));
            return;
        }
        stop(event.entity);
    }
    if (event.action == Action::Pause) return;
    auto resource = resources_->load(source.path);
    if (resource->kind != vox::ResourceKind::Audio) throw std::runtime_error("Choose a .wav or .mp3 audio file.");
    auto clip = clips_[resource.get()].lock();
    if (!clip) {
        clip = std::make_shared<Clip>(); clip->resource = resource;
        FMOD_CREATESOUNDEXINFO info{}; info.cbsize = sizeof(info);
        info.length = static_cast<unsigned int>(resource->bytes.size());
        check(system_->createSound(reinterpret_cast<const char*>(resource->bytes.data()),
            FMOD_OPENMEMORY | FMOD_2D, &info, &clip->sound));
        clips_[resource.get()] = clip;
    }
    Voice voice{clip, nullptr, source.path};
    check(system_->playSound(clip->sound, group(source.channel), true, &voice.channel));
    try {
        configure(voice, source);
        check(voice.channel->setPaused(false));
        voices_.emplace(event.entity, voice);
    } catch (...) { voice.channel->stop(); throw; }
}
void System::update(double) {
    if (!system_) return;
    VOX_PROFILE_SCOPE_C("Audio update", profile::colour::kAudio);
    // Lifecycle maintenance runs each frame; playback requests arrive as events.
    for (auto it = voices_.begin(); it != voices_.end();) {
        bool playing{};
        auto* source = registry_->try_get<cpn::AudioSource>(it->first);
        if (!source || source->path != it->second.path ||
            it->second.channel->isPlaying(&playing) != FMOD_OK || !playing) {
            it->second.channel->stop(); it = voices_.erase(it);
        } else {
            try { configure(it->second, *source); }
            catch (const std::exception& e) {
                std::cerr << e.what() << '\n';
                it->second.channel->stop(); it = voices_.erase(it); continue;
            }
            ++it;
        }
    }
    if (!paused_) for (auto entity : registry_->view<cpn::AudioSource>()) {
        const auto& source = registry_->get<cpn::AudioSource>(entity);
        if (source.playOnAwake && !source.path.empty() && !awakened_.contains(entity)) {
            try { bus_->publish(SourceCommand{entity, Action::Play}); }
            catch (const std::exception& e) { std::cerr << "[Audio] " << source.path << ": " << e.what() << '\n'; }
        }
    }
    std::erase_if(clips_, [](const auto& entry) { return entry.second.expired(); });
    check(system_->update());
}
void System::shutdown() {
    if (bus_) for (auto id : subscriptions_) bus_->unsubscribe(id);
    subscriptions_.clear(); bus_ = nullptr;
    if (registry_) registry_->on_destroy<cpn::AudioSource>().disconnect<&System::removed>(*this);
    registry_ = nullptr;
    if (master_) master_->stop();
    voices_.clear(); clips_.clear(); awakened_.clear();
    for (auto*& g : groups_) { if (g) g->release(); g = nullptr; }
    master_ = nullptr;
    if (system_) { system_->release(); system_ = nullptr; }
    resources_ = nullptr;
}
}
