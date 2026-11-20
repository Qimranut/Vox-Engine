#include "text_renderer.hpp"
#include "text/text_layout.hpp"
#include "components/component_types/transform.hpp"
#include "components/component_types/visibility.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

namespace graphics {
namespace {
GLuint shader(GLenum type, const char* source) {
    const auto id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr); glCompileShader(id);
    GLint ok{}; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]{}; glGetShaderInfoLog(id, sizeof(log), nullptr, log); glDeleteShader(id);
        throw std::runtime_error(std::string("Text shader: ") + log);
    }
    return id;
}
// This pass is used by both the Game and Editor framebuffer. Restore every state it changes.
struct State {
    GLint program{}, vao{}, buffer{}, active{}, texture{}, sampler{}, srcRGB{}, dstRGB{}, srcA{}, dstA{}, eqRGB{}, eqA{}, depthFunc{};
    GLboolean blend{}, depth{}, cull{}, depthMask{};
    State() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer); glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture); glGetIntegeri_v(GL_SAMPLER_BINDING, 0, &sampler);
        glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB); glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA); glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &eqRGB); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqA);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc); glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        blend = glIsEnabled(GL_BLEND); depth = glIsEnabled(GL_DEPTH_TEST); cull = glIsEnabled(GL_CULL_FACE);
    }
    ~State() {
        glUseProgram(program); glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBindTexture(GL_TEXTURE_2D, texture); glBindSampler(0, sampler); glActiveTexture(active);
        glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA); glBlendEquationSeparate(eqRGB, eqA);
        glDepthMask(depthMask); glDepthFunc(depthFunc);
        blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        depth ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        cull ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    }
};
}
TextRenderer::TextRenderer(vox::ResourceManager& resources) : resources_(resources) {
    const char* vs = R"(#version 450 core
layout(location=0) in vec4 vertex;
uniform mat4 transform;
out vec2 uv;
void main() { uv=vertex.zw; gl_Position=transform*vec4(vertex.xy,0,1); }
)";
    const char* fs = R"(#version 450 core
layout(binding=0) uniform sampler2D atlas;
uniform vec4 tint;
in vec2 uv;
out vec4 color;
void main() { float a=texture(atlas,uv).r*tint.a; if(a<0.001) discard; color=vec4(tint.rgb,a); }
)";
    GLuint vertex{}, fragment{};
    try {
        vertex = shader(GL_VERTEX_SHADER, vs); fragment = shader(GL_FRAGMENT_SHADER, fs);
        program_ = glCreateProgram(); glAttachShader(program_, vertex); glAttachShader(program_, fragment); glLinkProgram(program_);
        GLint ok{}; glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok) throw std::runtime_error("Cannot link text shader.");
    } catch (...) { glDeleteShader(vertex); glDeleteShader(fragment); glDeleteProgram(program_); throw; }
    glDeleteShader(vertex); glDeleteShader(fragment);
    matrixLocation_ = glGetUniformLocation(program_, "transform"); colorLocation_ = glGetUniformLocation(program_, "tint");
    glCreateVertexArrays(1, &vao_); glCreateBuffers(1, &vbo_);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(glm::vec4));
    glEnableVertexArrayAttrib(vao_, 0); glVertexArrayAttribFormat(vao_, 0, 4, GL_FLOAT, GL_FALSE, 0); glVertexArrayAttribBinding(vao_, 0, 0);
}
TextRenderer::~TextRenderer() {
    glDeleteBuffers(1, &vbo_); glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
}
TextRenderer::Atlas& TextRenderer::acquire(const std::string& path) {
    const auto resource = resources_.load(path);
    if (resource->kind != vox::ResourceKind::Font || !resource->font) throw std::runtime_error("Not a font: " + path);
    auto& entry = atlases_[path];
    if (entry && entry->resource == resource) return *entry;
    auto next = std::make_unique<Atlas>(); next->resource = resource;
    const auto& f = *resource->font;
    glCreateTextures(GL_TEXTURE_2D, 1, &next->texture);
    glTextureStorage2D(next->texture, 1, GL_R8, f.width, f.height);
    GLint alignment{}, rowLength{}, skipRows{}, skipPixels{}, buffer{};
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment); glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows); glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glTextureSubImage2D(next->texture, 0, 0, 0, f.width, f.height, GL_RED, GL_UNSIGNED_BYTE, f.pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment); glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows); glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
    glTextureParameteri(next->texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(next->texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(next->texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(next->texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    entry = std::move(next); return *entry;
}
void TextRenderer::render(const entt::registry& registry, const camera::Camera& camera, int width, int height) {
    if (width <= 0 || height <= 0) return;
    std::vector<entt::entity> draws;
    std::unordered_set<std::string> used;
    for (auto [e, t] : registry.view<cpn::Text>().each()) {
        used.insert(t.font);
        const auto* v = registry.try_get<cpn::Visibility>(e);
        if ((v && !v->visible_) || t.font.empty() || t.content.empty() || (!t.screenSpace && !registry.all_of<cpn::Transform>(e))) continue;
        draws.push_back(e);
    }
    std::erase_if(atlases_, [&](const auto& entry) { return !used.contains(entry.first); });
    std::erase_if(reported_, [&](const auto& path) { return !used.contains(path); });
    if (draws.empty()) return;
    // World text blends back-to-front against opaque scene depth; screen UI is drawn last.
    std::sort(draws.begin(), draws.end(), [&](auto a, auto b) {
        const auto& ta = registry.get<cpn::Text>(a); const auto& tb = registry.get<cpn::Text>(b);
        if (ta.screenSpace != tb.screenSpace) return !ta.screenSpace;
        if (!ta.screenSpace) {
            const float za = (camera.view() * glm::vec4(registry.get<cpn::Transform>(a).pos_, 1)).z;
            const float zb = (camera.view() * glm::vec4(registry.get<cpn::Transform>(b).pos_, 1)).z;
            if (za != zb) return za < zb;
        }
        return entt::to_integral(a) < entt::to_integral(b);
    });
    State state;
    glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE); glDepthMask(GL_FALSE); glDepthFunc(GL_LEQUAL);
    glUseProgram(program_); glBindVertexArray(vao_); glBindSampler(0, 0);
    for (const auto e : draws) {
        const auto& t = registry.get<cpn::Text>(e);
        try {
            auto& atlas = acquire(t.font); reported_.erase(t.font);
            const auto vertices = text::layout(*atlas.resource->font, t);
            if (vertices.empty()) continue;
            glm::mat4 matrix;
            if (t.screenSpace) {
                glDisable(GL_DEPTH_TEST);
                matrix = glm::ortho(0.f, static_cast<float>(width), static_cast<float>(height), 0.f) *
                    glm::translate(glm::mat4(1), glm::vec3(t.screenPosition, 0));
            } else {
                glEnable(GL_DEPTH_TEST);
                const auto& tf = registry.get<cpn::Transform>(e);
                matrix = camera.viewProjection() * glm::translate(glm::mat4(1), tf.pos_) * glm::mat4_cast(tf.rot_) *
                    glm::scale(glm::mat4(1), tf.scale_ * glm::vec3(1,-1,1));
            }
            glUniformMatrix4fv(matrixLocation_, 1, GL_FALSE, glm::value_ptr(matrix)); glUniform4fv(colorLocation_, 1, glm::value_ptr(t.color));
            glBindTextureUnit(0, atlas.texture);
            glNamedBufferData(vbo_, static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec4)), vertices.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        } catch (const std::exception& error) {
            if (reported_.insert(t.font).second) std::cerr << "[Text] " << error.what() << '\n';
        }
    }
}
}
