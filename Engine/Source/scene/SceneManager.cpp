#include "SceneManager.hpp"
#include "scene_events.hpp"
#include "events/bus.hpp"
#include "profile/profiler.hpp"
#include "components/component_types/visibility.hpp"
#include "components/component_types/render_material.hpp"
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <stdexcept>

namespace vox {
namespace {
void validate(const SceneDocument& d) {
    if (d.name.empty() || d.name.size() > 1024 || d.objects.size() > 100000 || d.assets.size() > 10000)
        throw std::runtime_error("Invalid scene name or scene exceeds supported size.");
    if (d.world.size() > 1024 || d.world.find("..") != std::string::npos)
        throw std::runtime_error("Invalid world map path.");
    std::set<uint32_t> ids;
    for (const auto& o : d.objects) {
        if (!o.id || !ids.insert(o.id).second || o.name.empty() || o.name.size() > 1024)
            throw std::runtime_error("Scene objects need unique nonzero IDs and valid names.");
        if (o.hasAudioSource && (!std::isfinite(o.audio.volume) || o.audio.volume < 0 || o.audio.volume > 1 ||
            static_cast<unsigned>(o.audio.channel) > 2 || o.audio.path.size() > 1024))
            throw std::runtime_error("Invalid audio volume, channel or asset path.");
        if (o.hasText) {
            const auto& t = o.text;
            if (t.font.size() > 1024 || t.content.size() > 4096 || !std::isfinite(t.size) || t.size <= 0 || t.size > 4096 ||
                t.alignment < 0 || t.alignment > 2 || !std::isfinite(t.screenPosition.x) || !std::isfinite(t.screenPosition.y))
                throw std::runtime_error("Invalid Text component.");
            for (int c = 0; c < 4; ++c)
                if (!std::isfinite(t.color[c]) || t.color[c] < 0 || t.color[c] > 1) throw std::runtime_error("Invalid text colour.");
        }
        if (o.hasTransform && !std::isfinite(o.transform.rot_.w))
            throw std::runtime_error("Transform values must be finite; scale cannot be zero.");
        for (int i = 0; o.hasTransform && i < 3; ++i)
            if (!std::isfinite(o.transform.pos_[i]) || !std::isfinite(o.transform.rot_[i]) ||
                !std::isfinite(o.transform.scale_[i]) || std::abs(o.transform.scale_[i]) < 0.0001f)
                throw std::runtime_error("Transform values must be finite; scale cannot be zero.");
    }
}
void writeVec(std::ostream& out, const glm::vec3& v) { out << v.x << ' ' << v.y << ' ' << v.z << ' '; }
void readVec(std::istream& in, glm::vec3& v) { in >> v.x >> v.y >> v.z; }
// Scene files store rotation as Euler degrees; Transform holds a quaternion.
void writeEuler(std::ostream& out, const glm::quat& q) { writeVec(out, glm::degrees(glm::eulerAngles(q))); }
void readEuler(std::istream& in, glm::quat& q) { glm::vec3 degrees{}; readVec(in, degrees); q = glm::quat(glm::radians(degrees)); }
void applyComponents(entt::registry& registry, entt::entity e, const SceneObject& o) {
    if (o.hasTransform) registry.emplace_or_replace<cpn::Transform>(e, o.transform);
    else registry.remove<cpn::Transform>(e);
    if (o.hasVisibility) registry.emplace_or_replace<cpn::Visibility>(e, cpn::Visibility{o.visible});
    else registry.remove<cpn::Visibility>(e);
    if (o.hasAudioSource) registry.emplace_or_replace<cpn::AudioSource>(e, o.audio);
    else registry.remove<cpn::AudioSource>(e);
    if (o.hasText) registry.emplace_or_replace<cpn::Text>(e, o.text);
    else registry.remove<cpn::Text>(e);
    // The texture path is the object's albedo; graphics resolves it to a GPU texture when drawing.
    if (!o.texture.empty()) registry.emplace_or_replace<cpn::RenderMaterial>(e, cpn::RenderMaterial{o.texture});
    else registry.remove<cpn::RenderMaterial>(e);
}
std::vector<ResourceHandle> loadResources(ResourceManager& resources, const SceneDocument& d) {
    std::vector<ResourceHandle> held;
    for (const auto& p : d.assets) held.push_back(resources.load(p));
    for (const auto& o : d.objects) {
        const auto add = [&](const std::string& p, ResourceKind kind) {
            if (p.empty()) return;
            auto r = resources.load(p);
            if (r->kind != kind) throw std::runtime_error("Incorrect asset type: " + p);
            held.push_back(std::move(r));
        };
        add(o.model, ResourceKind::Model);
        add(o.texture, ResourceKind::Texture);
        if (o.hasAudioSource) add(o.audio.path, ResourceKind::Audio);
        if (o.hasText) add(o.text.font, ResourceKind::Font);
    }
    return held;
}
}
SceneManager::SceneManager(entt::registry& registry, ResourceManager& resources, ::events::Bus& bus)
    : registry_(registry), resources_(resources), bus_(bus) {}
SceneManager::~SceneManager() { unload(); }
// Publishes only on an actual change, so Editor undo/redo of object edits never reloads the world.
void SceneManager::announceWorld() noexcept {
    if (document_.world == activeWorld_) return;
    activeWorld_ = document_.world;
    try { bus_.publish(events::SceneWorldChanged{activeWorld_}); } catch (...) {}
}
void SceneManager::set2D(bool enabled) {
    if (!loaded_) throw std::runtime_error("No scene is loaded.");
    if (document_.mode2D != enabled) { document_.mode2D = enabled; dirty_ = true; }
}
void SceneManager::setWorld(const std::string& mapPath) {
    if (!loaded_) throw std::runtime_error("No scene is loaded.");
    if (mapPath.size() > 1024 || mapPath.find("..") != std::string::npos) throw std::runtime_error("Invalid world map path.");
    if (document_.world == mapPath) return;
    document_.world = mapPath; dirty_ = true; announceWorld();
}
SceneDocument SceneManager::read(const std::filesystem::path& path) {
    VOX_PROFILE_SCOPE_C("Scene read", profile::colour::kResource);
    if (std::filesystem::file_size(path) > 32ull * 1024 * 1024) throw std::runtime_error("Scene exceeds 32 MiB.");
    std::ifstream in(path); in.imbue(std::locale::classic());
    if (!in) throw std::runtime_error("Cannot open scene: " + path.string());
    SceneDocument d; std::string tag; int version{}; size_t count{};
    in >> tag >> version;
    if (tag != "VOXSCENE" || version < 1 || version > 6)
        throw std::runtime_error("Unsupported scene format; expected VOXSCENE 1 to 6.");
    in >> std::quoted(d.name) >> tag >> count;
    if (!in || tag != "ASSETS" || count > 10000) throw std::runtime_error("Invalid scene asset list.");
    for (size_t i = 0; i < count; ++i) { std::string p; in >> std::quoted(p); d.assets.push_back(p); }
    in >> tag;
    // VOXSCENE 4: an optional WORLD "maps/x.voxmap" line between ASSETS and OBJECTS.
    if (version >= 4 && tag == "WORLD") { in >> std::quoted(d.world) >> tag; }
    if (version >= 6) {
        int mode{};
        if (tag != "MODE") throw std::runtime_error("Missing scene mode.");
        in >> mode >> tag;
        if (!in || (mode != 2 && mode != 3)) throw std::runtime_error("Invalid scene mode.");
        d.mode2D = mode == 2;
    }
    in >> count;
    if (!in || tag != "OBJECTS" || count > 100000) throw std::runtime_error("Invalid scene object list.");
    for (size_t i = 0; i < count; ++i) {
        SceneObject o; int visible{};
        in >> o.id >> std::quoted(o.name);
        if (version >= 2) {
            int transform{}, visibility{};
            in >> transform >> visibility;
            if (!in || (transform != 0 && transform != 1) || (visibility != 0 && visibility != 1))
                throw std::runtime_error("Invalid component presence flags.");
            o.hasTransform = transform != 0; o.hasVisibility = visibility != 0;
        }
        in >> visible;
        readVec(in, o.transform.pos_); readEuler(in, o.transform.rot_); readVec(in, o.transform.scale_);
        in >> std::quoted(o.model) >> std::quoted(o.texture);
        if (version >= 3) {
            int present{}, channel{}, loop{}, awake{};
            in >> present >> std::quoted(o.audio.path) >> channel >> o.audio.volume >> loop >> awake;
            if (!in || (present != 0 && present != 1) || channel < 0 || channel > 2 ||
                (loop != 0 && loop != 1) || (awake != 0 && awake != 1))
                throw std::runtime_error("Malformed Audio Source component.");
            o.hasAudioSource = present != 0; o.audio.channel = static_cast<cpn::AudioChannel>(channel);
            o.audio.loop = loop != 0; o.audio.playOnAwake = awake != 0;
        }
        if (version >= 5) {
            int present{}, screen{};
            in >> present >> std::quoted(o.text.font) >> std::quoted(o.text.content) >> screen >> o.text.size
               >> o.text.screenPosition.x >> o.text.screenPosition.y >> o.text.color.r >> o.text.color.g
               >> o.text.color.b >> o.text.color.a >> o.text.alignment;
            if (!in || (present != 0 && present != 1) || (screen != 0 && screen != 1))
                throw std::runtime_error("Malformed Text component.");
            o.hasText = present != 0; o.text.screenSpace = screen != 0;
        }
        if (!in || (visible != 0 && visible != 1)) throw std::runtime_error("Malformed scene object.");
        o.visible = visible != 0; d.objects.push_back(std::move(o));
    }
    in >> std::ws;
    if (!in.eof()) throw std::runtime_error("Unexpected trailing scene content.");
    validate(d); return d;
}
void SceneManager::replace(const SceneDocument& d) {
    VOX_PROFILE_SCOPE_C("Scene replace", profile::colour::kResource);
    validate(d);
    auto next = d;
    std::unordered_map<uint32_t, entt::entity> staged;
    std::vector<ResourceHandle> held;
    try {
        held = loadResources(resources_, d);
        staged.reserve(d.objects.size());
        for (const auto& o : d.objects) {
            auto e = registry_.create();
            try { staged.emplace(o.id, e); }
            catch (...) { registry_.destroy(e); throw; }
            applyComponents(registry_, e, o);
        }
    } catch (...) {
        for (const auto& [id, e] : staged) registry_.destroy(e);
        held.clear(); resources_.collectUnused(); throw;
    }
    // Replacing a document (including undo) must not publish a temporary empty
    // world: that would unload terrain and discard unsaved edits to the same map.
    for (const auto& [id, e] : entities_) if (registry_.valid(e)) registry_.destroy(e);
    entities_.clear(); resourcesHeld_.clear();
    document_ = std::move(next); entities_ = std::move(staged); resourcesHeld_ = std::move(held);
    loaded_ = true; dirty_ = true;
    resources_.collectUnused();
    announceWorld();
}
void SceneManager::load(const std::filesystem::path& path) {
    auto absolute = std::filesystem::absolute(path);
    replace(read(absolute)); path_ = std::move(absolute); dirty_ = false;
}
void SceneManager::newScene() {
    replace(SceneDocument{}); path_.clear(); dirty_ = false;
}
SceneDocument SceneManager::snapshot() const {
    auto d = document_;
    for (auto& o : d.objects) {
        auto e = entity(o.id);
        if (registry_.valid(e)) {
            o.hasTransform = registry_.all_of<cpn::Transform>(e);
            o.hasVisibility = registry_.all_of<cpn::Visibility>(e);
            o.hasAudioSource = registry_.all_of<cpn::AudioSource>(e);
            o.hasText = registry_.all_of<cpn::Text>(e);
            if (auto* t = registry_.try_get<cpn::Text>(e)) o.text = *t;
            if (auto* a = registry_.try_get<cpn::AudioSource>(e)) o.audio = *a;
            if (auto* t = registry_.try_get<cpn::Transform>(e)) o.transform = *t;
            if (auto* v = registry_.try_get<cpn::Visibility>(e)) o.visible = v->visible_;
            auto* m = registry_.try_get<cpn::RenderMaterial>(e); o.texture = m ? m->albedo_ : std::string{};
        }
    }
    return d;
}
void SceneManager::save(const std::filesystem::path& path) {
    VOX_PROFILE_SCOPE_C("Scene save", profile::colour::kResource);
    if (!loaded_) throw std::runtime_error("No scene is loaded.");
    auto d = snapshot(); validate(d);
    auto absolute = std::filesystem::absolute(path);
    if (absolute.extension() != ".voxscene") throw std::runtime_error("Use the .voxscene file extension.");
    std::filesystem::create_directories(absolute.parent_path());
    auto temp = absolute; temp += ".tmp";
    try {
        std::ofstream out(temp, std::ios::trunc); out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<float>::max_digits10);
        out << "VOXSCENE 6\n" << std::quoted(d.name) << "\nASSETS " << d.assets.size() << '\n';
        for (const auto& p : d.assets) out << std::quoted(p) << '\n';
        out << "WORLD " << std::quoted(d.world) << '\n';
        out << "MODE " << (d.mode2D ? 2 : 3) << '\n';
        out << "OBJECTS " << d.objects.size() << '\n';
        for (const auto& o : d.objects) {
            out << o.id << ' ' << std::quoted(o.name) << ' '
                << int(o.hasTransform) << ' ' << int(o.hasVisibility) << ' ' << int(o.visible) << ' ';
            writeVec(out, o.transform.pos_); writeEuler(out, o.transform.rot_); writeVec(out, o.transform.scale_);
            out << std::quoted(o.model) << ' ' << std::quoted(o.texture) << ' '
                << int(o.hasAudioSource) << ' ' << std::quoted(o.audio.path) << ' '
                << static_cast<int>(o.audio.channel) << ' ' << o.audio.volume << ' '
                << int(o.audio.loop) << ' ' << int(o.audio.playOnAwake) << ' '
                << int(o.hasText) << ' ' << std::quoted(o.text.font) << ' ' << std::quoted(o.text.content) << ' '
                << int(o.text.screenSpace) << ' ' << o.text.size << ' ' << o.text.screenPosition.x << ' ' << o.text.screenPosition.y << ' '
                << o.text.color.r << ' ' << o.text.color.g << ' ' << o.text.color.b << ' ' << o.text.color.a << ' ' << o.text.alignment << '\n';
        }
        out.flush(); if (!out) throw std::runtime_error("Failed writing scene.");
        out.close(); if (!out) throw std::runtime_error("Failed closing scene.");
        if (!MoveFileExW(temp.c_str(), absolute.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not replace scene file; the existing save was preserved.");
    } catch (...) { std::error_code ec; std::filesystem::remove(temp, ec); throw; }
    path_ = std::move(absolute); dirty_ = false;
}
void SceneManager::unload() noexcept {
    for (const auto& [id, e] : entities_) if (registry_.valid(e)) registry_.destroy(e);
    entities_.clear(); resourcesHeld_.clear(); document_.objects.clear(); document_.assets.clear();
    document_.world.clear(); document_.mode2D = false;
    path_.clear(); loaded_ = dirty_ = false; resources_.collectUnused();
    announceWorld();
}
entt::entity SceneManager::entity(uint32_t id) const {
    auto it = entities_.find(id); return it == entities_.end() ? entt::null : it->second;
}
uint32_t SceneManager::create(const std::string& name) {
    auto d = snapshot(); uint32_t id = 1;
    for (const auto& o : d.objects) {
        if (o.id == UINT32_MAX) throw std::runtime_error("Scene object ID limit reached.");
        id = std::max(id, o.id + 1);
    }
    SceneObject object; object.id = id; object.name = name; d.objects.push_back(object);
    validate(d);
    auto e = registry_.create();
    try {
        registry_.emplace<cpn::Transform>(e, object.transform);
        registry_.emplace<cpn::Visibility>(e, cpn::Visibility{object.visible});
        entities_.emplace(id, e);
    } catch (...) { registry_.destroy(e); throw; }
    document_ = std::move(d); loaded_ = dirty_ = true; return id;
}
void SceneManager::erase(uint32_t id) {
    auto d = snapshot(); std::erase_if(d.objects, [id](const auto& o) { return o.id == id; });
    auto held = loadResources(resources_, d);
    if (auto it = entities_.find(id); it != entities_.end()) {
        if (registry_.valid(it->second)) registry_.destroy(it->second);
        entities_.erase(it);
    }
    document_ = std::move(d); resourcesHeld_ = std::move(held); dirty_ = true;
    resources_.collectUnused();
}
void SceneManager::update(const SceneObject& object) {
    auto d = snapshot();
    auto it = std::find_if(d.objects.begin(), d.objects.end(), [&](const auto& o) { return o.id == object.id; });
    if (it == d.objects.end()) throw std::runtime_error("Object no longer exists.");
    *it = object;
    validate(d);
    // Resolve assets before changing ECS data; editing one object preserves other voices.
    auto held = loadResources(resources_, d);
    auto e = entity(object.id);
    if (!registry_.valid(e)) throw std::runtime_error("Scene entity no longer exists.");
    applyComponents(registry_, e, object);
    document_ = std::move(d); resourcesHeld_ = std::move(held); dirty_ = true;
    resources_.collectUnused();
}
void SceneManager::setComponent(uint32_t id, SceneComponent component, bool present) {
    auto d = snapshot();
    auto it = std::find_if(d.objects.begin(), d.objects.end(), [id](const auto& o) { return o.id == id; });
    if (it == d.objects.end()) throw std::runtime_error("Object no longer exists.");
    bool* flag = nullptr;
    switch (component) {
    case SceneComponent::Transform: flag = &it->hasTransform; break;
    case SceneComponent::Visibility: flag = &it->hasVisibility; break;
    case SceneComponent::AudioSource: flag = &it->hasAudioSource; break;
    case SceneComponent::Text: flag = &it->hasText; break;
    default: throw std::runtime_error("Unknown component type.");
    }
    if (*flag == present) throw std::runtime_error(present ? "Component already exists." : "Component is not present.");
    *flag = present;
    if (present && component == SceneComponent::Transform) it->transform = cpn::Transform{};
    if (present && component == SceneComponent::Visibility) it->visible = true;
    if (present && component == SceneComponent::AudioSource) it->audio = cpn::AudioSource{};
    if (present && component == SceneComponent::Text) it->text = cpn::Text{};
    update(*it);
}
}
