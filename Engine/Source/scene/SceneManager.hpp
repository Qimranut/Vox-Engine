#pragma once
#include "resources/ResourceManager.hpp"
#include "components/component_types/transform.hpp"
#include "components/component_types/audio_source.hpp"
#include <entt.hpp>
#include "components/component_types/text.hpp"

namespace events { class Bus; }

namespace vox {
enum class SceneComponent { Transform, Visibility, AudioSource, Text };
struct SceneObject {
    uint32_t id{};
    std::string name{"Object"};
    cpn::Transform transform;
    bool visible{true};
    bool hasTransform{true}, hasVisibility{true};
    std::string model, texture;
    bool hasAudioSource{};
    cpn::AudioSource audio;
    bool hasText{};
    cpn::Text text;
};
struct SceneDocument {
    std::string name{"Untitled"};
    std::vector<SceneObject> objects;
    std::vector<std::string> assets;
    // Asset-relative .voxmap the scene plays in; empty for none. VOXSCENE 4.
    std::string world;
    bool mode2D{}; // Camera/editing mode; never converts or deletes scene content.
};
// Shared runtime service: owns only this scene's entities, never clears the whole registry.
// The voxel world is not an entity of the scene: SceneManager publishes SceneWorldChanged
// on the bus whenever `world` changes and voxel::System loads or unloads in response.
class SceneManager {
public:
    SceneManager(entt::registry& registry, ResourceManager& resources, ::events::Bus& bus);
    ~SceneManager();
    void load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path);
    void replace(const SceneDocument& document);
    void newScene();
    void unload() noexcept;
    SceneDocument snapshot() const;
    static SceneDocument read(const std::filesystem::path& path);
    uint32_t create(const std::string& name = "Object");
    void erase(uint32_t id);
    void update(const SceneObject& object);
    void setComponent(uint32_t id, SceneComponent component, bool present);
    // Points the scene at a map (or "" for none), marks it dirty and publishes SceneWorldChanged.
    void setWorld(const std::string& mapPath);
    void set2D(bool enabled);
    bool is2D() const { return document_.mode2D; }
    const std::string& world() const { return document_.world; }
    entt::entity entity(uint32_t id) const;
    const auto& objects() const { return document_.objects; }
    const auto& path() const { return path_; }
    const auto& name() const { return document_.name; }
    bool loaded() const { return loaded_; }
    bool dirty() const { return dirty_; }
    void setDirty(bool value) { dirty_ = value; }
private:
    void announceWorld() noexcept;
    entt::registry& registry_;
    ResourceManager& resources_;
    ::events::Bus& bus_;
    SceneDocument document_;
    std::string activeWorld_;
    std::unordered_map<uint32_t, entt::entity> entities_;
    std::vector<ResourceHandle> resourcesHeld_;
    std::filesystem::path path_;
    bool loaded_{}, dirty_{};
};
}
