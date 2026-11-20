
#include "renderer.hpp"
#include "components/component_types/text.hpp"
#include "../../window/sys/win_sys.hpp"
#include <glm/gtc/color_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../../graphics/model/model.hpp"
#include "../texture/sampler.hpp"
#include "../texture/texture_manager.hpp"
#include "../../components/component_types/render_material.hpp"

// stds
#include <algorithm>
#include <iostream>

namespace graphics::renderer
{
    Renderer::Renderer(window::System& window)
        : window_(window)
    {
        // Set initial viewport. Framebuffer size, not window size - they differ on HiDPI.
        GLFWwindow* focusedWindow = window_.focused_window();
        if (focusedWindow) {
            int width, height;
            glfwGetFramebufferSize(focusedWindow, &width, &height);
            glViewport(0, 0, width, height);
        }

        // Fixed pipeline state, set once. The cube in buildCubeModel() is wound
        // CCW-outward on all six faces, so back-face culling is safe.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        // Colour pipeline: textures decode sRGB -> linear in the sampler,
        // lighting runs in linear, and the mesh fragment shader encodes
        // linear -> sRGB exactly once. Both render targets therefore store
        // display-ready bytes as-is - the Game's default framebuffer and the
        // Editor's RGBA8 viewport texture, which ImGui shows unchanged. WGL
        // often hands out an sRGB-capable default framebuffer anyway, so turn
        // the automatic encode off explicitly or it would be applied twice.
        // If the scene ever renders to an SRGB8_ALPHA8 or HDR target, remove
        // the shader's encode and move it to that final pass.
        glDisable(GL_FRAMEBUFFER_SRGB);

        // Display-referred, like the debug line colours - not converted.
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    }

    void Renderer::clear()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void Renderer::setClearColor(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
    }

    void Renderer::swapBuffers()
    {
        GLFWwindow* focusedWindow = window_.focused_window();
        if (focusedWindow) {
            glfwSwapBuffers(focusedWindow);
        }
    }



    void Renderer::setShaderProgram(GLuint programID)
    {
        shaderProgram_ = programID;
        cacheUniformLocations();
    }

    void Renderer::cacheUniformLocations()
    {
        if (shaderProgram_ == 0) {
            locViewProj_ = -1;
            locModel_ = -1;
            locTint_ = -1;
            locLightingEnabled_ = -1;
            locTransparent2D_ = -1;

            locNormalMatrix_ = -1;
            locCameraPos_ = -1;
            locLightDir_ = -1;
            locLightColor_ = -1;
            locLightIntensity_ = -1;
            locMetallic_ = -1;
            locRoughness_ = -1;
            locAo_ = -1;
            locHasNormalMap_ = -1;
            return;
        }

        locViewProj_ = glGetUniformLocation(shaderProgram_, "u_viewProj");
        locModel_ = glGetUniformLocation(shaderProgram_, "u_model");
        locTint_ = glGetUniformLocation(shaderProgram_, "u_tint");
        locLightingEnabled_ = glGetUniformLocation(shaderProgram_, "u_lightingEnabled");
        locTransparent2D_ = glGetUniformLocation(shaderProgram_, "u_transparent2D");

        locNormalMatrix_ = glGetUniformLocation(shaderProgram_, "u_normalMatrix");
        locCameraPos_ = glGetUniformLocation(shaderProgram_, "u_cameraPos");
        locLightDir_ = glGetUniformLocation(shaderProgram_, "u_lightDir");
        locLightColor_ = glGetUniformLocation(shaderProgram_, "u_lightColor");
        locLightIntensity_ = glGetUniformLocation(shaderProgram_, "u_lightIntensity");
        locMetallic_ = glGetUniformLocation(shaderProgram_, "u_metallic");
        locRoughness_ = glGetUniformLocation(shaderProgram_, "u_roughness");
        locAo_ = glGetUniformLocation(shaderProgram_, "u_ao");
        locHasNormalMap_ = glGetUniformLocation(shaderProgram_, "u_hasNormalMap");

        if (locViewProj_ < 0)
            std::cerr << "[Renderer] shader has no u_viewProj uniform\n";

        if (locModel_ < 0)
            std::cerr << "[Renderer] shader has no u_model uniform\n";
    }

    void Renderer::setTextureSources(texture::TextureManager& textures, const texture::Sampler& albedoSampler)
    {
        textures_ = &textures;
        albedoSampler_ = &albedoSampler;
    }

    void Renderer::bindAlbedo(texture::TextureHandle handle)
    {
        if (textures_)
            textures_->get(handle).bind(texture::TextureUnit::Albedo);
    }


    void Renderer::bindNormal(texture::TextureHandle handle)
    {
        if (textures_)
            textures_->get(handle).bind(texture::TextureUnit::Normal);
    }

    void Renderer::beginFrame(const camera::Camera& cam, bool lightingEnabled, bool transparent2D)
    {
        if (shaderProgram_ == 0) return;

        glUseProgram(shaderProgram_);

        transparent2D_ = transparent2D;
        if (locTransparent2D_ >= 0) glUniform1i(locTransparent2D_, transparent2D ? 1 : 0);
        if (locLightingEnabled_ >= 0)
            glUniform1i(locLightingEnabled_, lightingEnabled ? 1 : 0);

        if (locViewProj_ >= 0)
            glUniformMatrix4fv(locViewProj_, 1, GL_FALSE, glm::value_ptr(cam.viewProjection()));

        if (albedoSampler_)
            albedoSampler_->bind(texture::TextureUnit::Albedo);

        // Never leave the unit empty: sampling texture name 0 returns black.
        if (textures_)
            bindAlbedo(textures_->white());


        if (locCameraPos_ >= 0)
            glUniform3fv(locCameraPos_, 1, glm::value_ptr(cam.position()));

        if (locLightDir_ >= 0)
        {
            const glm::vec3 lightDir = glm::normalize(glm::vec3{ 0.4f, 1.0f, 0.3f });
            glUniform3fv(locLightDir_, 1, glm::value_ptr(lightDir));
        }

        if (locLightColor_ >= 0)
        {
            const glm::vec3 lightColor{ 1.0f };
            glUniform3fv(locLightColor_, 1, glm::value_ptr(lightColor));
        }

        if (locLightIntensity_ >= 0)
            glUniform1f(locLightIntensity_, 1.0f);

    }

    void Renderer::renderModel(const model::Model& model, const glm::mat4& modelMatrix, const glm::vec3& tint, float metallic, float roughness, float ao, bool hasNormalMap)
    {
        if (shaderProgram_ == 0) return;

        if (locModel_ >= 0)
            glUniformMatrix4fv(locModel_, 1, GL_FALSE, glm::value_ptr(modelMatrix));

        if (locMetallic_ >= 0)
            glUniform1f(locMetallic_, metallic);
        if (locRoughness_ >= 0)
            glUniform1f(locRoughness_, roughness);
        if (locAo_ >= 0)
            glUniform1f(locAo_, ao);
        if (locHasNormalMap_ >= 0)
            glUniform1i(locHasNormalMap_, hasNormalMap ? 1 : 0);

        if (locNormalMatrix_ >= 0)
        {
            glm::mat3 normalMatrix =
                glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
            glUniformMatrix3fv(locNormalMatrix_, 1, GL_FALSE,
                glm::value_ptr(normalMatrix));
        }

        // An unset vec3 uniform is zero, which would draw everything black.
        // Tints are picked by eye, so they are sRGB; lighting needs linear.
        // Converted here once per draw rather than per fragment.
        if (locTint_ >= 0)
            glUniform3fv(locTint_, 1, glm::value_ptr(glm::convertSRGBToLinear(tint)));

        for (const auto& mesh : model.meshes) {
            mesh.buffer->BindVAO();
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
            mesh.buffer->UnbindVAO();
        }
    }

    void Renderer::endFrame()
    {
        // Leave no mipmapped sampler on unit 0: the Editor's viewport texture
        // has one level, and would sample black under it.
        texture::Sampler::unbind(texture::TextureUnit::Albedo);
        glBindTextureUnit(static_cast<GLuint>(texture::TextureUnit::Albedo), 0);

        glUseProgram(0);
    }

    void Renderer::renderEntities(const ecs::ECSManager& ecs, const model::Model& model)
    {
        if (shaderProgram_ == 0) return;

        const entt::registry& registry = ecs.registry();

        drawList_.clear();

        for (auto [entity, tf, vis] : registry.view<cpn::Transform, cpn::Visibility>().each())
        {
            if (!vis.visible_ || registry.all_of<cpn::Text>(entity))
                continue;

            const glm::mat4 world =
                glm::translate(glm::mat4{ 1.f }, tf.pos_) *
                glm::mat4_cast(tf.rot_) *
                glm::scale(glm::mat4{ 1.f }, tf.scale_);

            texture::TextureHandle albedo;
            texture::TextureHandle normalMap;
            bool hasNormalMap = false;
            float metallic = 0.5f, roughness = 0.5f, ao = 1.f;

            if (textures_)
            {
                const cpn::RenderMaterial* material = registry.try_get<cpn::RenderMaterial>(entity);

                albedo = material && !material->albedo_.empty()
                    ? textures_->acquire(material->albedo_, texture::ColorSpace::Srgb)
                    : textures_->white();

                if (material && !material->normalMap_.empty())
                {
                    normalMap = textures_->acquire(material->normalMap_, texture::ColorSpace::Linear);
                    hasNormalMap = true;
                }

                if (material)
                {
                    metallic = material->metallic_;
                    roughness = material->roughness_;
                    ao = material->ao_;
                }
            }

            drawList_.push_back(Draw{ albedo, normalMap, hasNormalMap, world, vis.tint_,
                entt::to_integral(entity), metallic, roughness, ao });
        }

        // Menus use painter order: lower Z first, then entity ID for equal Z.
        // 3D keeps its original opaque texture batching and depth behaviour.
        if (transparent2D_)
            std::sort(drawList_.begin(), drawList_.end(), [](const Draw& a, const Draw& b) {
                if (a.world[3].z != b.world[3].z) return a.world[3].z < b.world[3].z;
                return a.entityId < b.entityId;
            });
        else
            std::sort(drawList_.begin(), drawList_.end(),
                [](const Draw& a, const Draw& b) { return a.albedo < b.albedo; });

        GLboolean blend{}, depth{}, cull{}, depthMask{};
        GLint srcRGB{}, dstRGB{}, srcAlpha{}, dstAlpha{}, eqRGB{}, eqAlpha{};
        if (transparent2D_) {
            blend = glIsEnabled(GL_BLEND); depth = glIsEnabled(GL_DEPTH_TEST); cull = glIsEnabled(GL_CULL_FACE);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
            glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB); glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha); glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
            glGetIntegerv(GL_BLEND_EQUATION_RGB, &eqRGB); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqAlpha);
            glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
        }

        bool anyBound = false;
        texture::TextureHandle bound;

        for (const Draw& draw : drawList_)
        {
            if (!anyBound || !(draw.albedo == bound))
            {
                bindAlbedo(draw.albedo);
                bound = draw.albedo;
                anyBound = true;
            }
            if (draw.hasNormalMap)
                bindNormal(draw.normalMap);

            renderModel(model, draw.world, draw.tint , draw.metallic, draw.roughness , draw.ao, draw.hasNormalMap);
        }
        if (transparent2D_) {
            glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha); glBlendEquationSeparate(eqRGB, eqAlpha);
            glDepthMask(depthMask);
            blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
            depth ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
            cull ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
        }
    }





















    Buffer::Buffer()
    {
        // Create 1 VAO
        glGenVertexArrays(1, &mVAO);
        // Create 1 VBO
        glGenBuffers(1, &mVBO);
        // Create 1 EBO
        glGenBuffers(1, &mEBO);
    }

    Buffer::~Buffer()
    {
        glDeleteBuffers(1, &mVBO);
        glDeleteBuffers(1, &mEBO);
        glDeleteVertexArrays(1, &mVAO);
    }

    Buffer::Buffer(Buffer&& other) noexcept
        : mVAO(other.mVAO),
        mVBO(other.mVBO),
        mEBO(other.mEBO)
    {
        other.mVAO = 0;
        other.mVBO = 0;
        other.mEBO = 0;
    }

    Buffer& renderer::Buffer::operator=(renderer::Buffer&& other) noexcept {

        if (this != &other) {

            glDeleteVertexArrays(1, &mVAO);
            glDeleteBuffers(1, &mVBO);
            glDeleteBuffers(1, &mEBO);

            mVAO = other.mVAO;
            mVBO = other.mVBO;
            mEBO = other.mEBO;

            other.mVAO = 0;
            other.mVBO = 0;
            other.mEBO = 0;
        }

        return *this;
    }

    void Buffer::BindVAO() const
    {
        glBindVertexArray(mVAO);
    }

    void Buffer::UnbindVAO() const
    {
        glBindVertexArray(0);
    }

    void Buffer::BindVBO() const
    {
        glBindBuffer(GL_ARRAY_BUFFER, mVBO);
    }

    void Buffer::BindEBO() const
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mEBO);
    }

    void Buffer::UnbindVBO() const
    {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void Buffer::SetData(GLsizeiptr size,
        const void* vertices,
        GLenum usage) const
    {
        glBufferData(GL_ARRAY_BUFFER, size, vertices, usage);
    }

    void Buffer::SetIndexData(GLsizeiptr size, const void* data, GLenum usage) const
    {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            size,
            data,
            usage
        );
    }

    void Buffer::SetAttribute(GLuint index,
        GLint size,
        GLenum type,
        GLboolean normalized,
        GLsizei stride,
        const void* offset) const
    {
        glVertexAttribPointer(
            index,
            size,
            type,
            normalized,
            stride,
            offset);

        glEnableVertexAttribArray(index);
    }

}


