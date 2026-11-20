#pragma once
#include "resources/ResourceManager.hpp"
#include "graphics/camera/camera.hpp"
#include <GL/glew.h>
#include <entt.hpp>
#include <unordered_set>

namespace graphics {
class TextRenderer {
public:
    explicit TextRenderer(vox::ResourceManager& resources);
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    void render(const entt::registry& registry, const camera::Camera& camera, int width, int height);
private:
    struct Atlas {
        GLuint texture{};
        vox::ResourceHandle resource;
        ~Atlas() { if (texture) glDeleteTextures(1, &texture); }
    };
    Atlas& acquire(const std::string& path);
    vox::ResourceManager& resources_;
    std::unordered_map<std::string, std::unique_ptr<Atlas>> atlases_;
    std::unordered_set<std::string> reported_;
    GLuint program_{}, vao_{}, vbo_{};
    GLint matrixLocation_{}, colorLocation_{};
};
}
