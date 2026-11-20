/*************************************************************************************************************
\file       renderer.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Renderer, the textured entity/model pass with per-frame camera upload and texture-sorted draws,
            and Buffer, a move-only VAO/VBO/EBO wrapper; also the global mesh Vertex layout.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

#include <GL/glew.h>
#include "../../ecs/ecs_manager/ecs_mng.hpp"
#include "../../window/sys/win_sys.hpp"
#include "../camera/camera.hpp"
#include "../texture/texture_types.hpp"
//#include "../../graphics/model/model.hpp"

// stds
#include <vector>

namespace graphics::model
{
    class Model;
}
namespace graphics::texture
{
    class TextureManager;
    class Sampler;
}
namespace graphics::renderer
{
    class Renderer
    {
    public:
        /*!
            @brief
                Sets the initial viewport to the focused window's framebuffer and the fixed
                pipeline state (depth test, back-face culling, no automatic sRGB encode, clear colour).
            @param window
                Window system the renderer draws to; must outlive the renderer
            */
        Renderer(window::System& window);

        /*!
            @brief
                Clears the colour and depth buffers.
            */
        void clear();

        /*!
            @brief
                Sets the clear colour (display-referred, not converted).
            @param r
                Red
            @param g
                Green
            @param b
                Blue
            @param a
                Alpha
            */
        void setClearColor(float r, float g, float b, float a);

        /*!
            @brief
                Swaps the focused window's buffers, if there is one.
            */
        void swapBuffers();

        /*!
            @brief
                Sets the mesh shader program and caches its uniform locations.
            @param programID
                Linked program, or 0 to disable drawing
            */
        void setShaderProgram(GLuint programID);

        /*!
            @brief
                Sets where textures come from and how the albedo unit samples them.
            @param textures
                Resolves material paths. Must outlive the renderer.
            @param albedoSampler
                Bound to the albedo unit for the whole frame. Must outlive the
                renderer.
            */
        void setTextureSources(texture::TextureManager& textures, const texture::Sampler& albedoSampler);

        /*!
            @brief
                Binds the shader program and uploads the camera's view-projection,
                once for the whole frame.

                Also binds the albedo sampler and the white texture, so renderModel()
                is valid straight away.
            @param cam
                Camera to render from. The renderer deliberately does not own
                camera state, it receives it - that is what lets the camera move
                into an ECS component later without touching this class.
            */
        // Menu scenes bypass object lighting while keeping texture colour conversion.
        void beginFrame(const camera::Camera& cam, bool lightingEnabled = true, bool transparent2D = false);

        /*!
            @brief
                Draws every mesh of a model.
            @param model
                Model whose meshes are drawn
            @param modelMatrix
                World transform of this instance
            @param tint
                Object colour, sRGB as authored. Converted to linear here.
            */
        void renderModel(const model::Model& model, const glm::mat4& modelMatrix,
                         const glm::vec3& tint = glm::vec3{ 1.f }, float metallic = 0.0f, float roughness = 0.5f, float ao = 1.0f, bool hasNormalMap = false);

        /*!
            @brief
                Unbinds the shader program, sampler and texture. Pairs with
                beginFrame().
            */
        void endFrame();


        /*!
            @brief
                Draws every visible entity that has a Transform, as the given model,
                textured by its RenderMaterial when it has one.

                Draws are sorted by texture so each texture is bound once per frame.
                Call between beginFrame() and endFrame().
            @param ecs
                ECS whose entities are drawn
            @param model
                Placeholder model until entities carry a model component
            */
        void renderEntities(const ecs::ECSManager& ecs, const model::Model& model);

    private:
        struct Draw
        {
            texture::TextureHandle albedo;
            texture::TextureHandle normalMap;
            bool hasNormalMap;
            glm::mat4 world;
            glm::vec3 tint;
            uint32_t entityId{};
            float metallic;
            float roughness;
            float ao;
        };

        window::System& window_;
        GLuint shaderProgram_ = 0;

        texture::TextureManager* textures_ = nullptr;
        const texture::Sampler* albedoSampler_ = nullptr;

        // Reused every frame so steady-state drawing allocates nothing.
        std::vector<Draw> drawList_;

        // Resolved once in setShaderProgram(), not per draw call.
        // glGetUniformLocation returns GLint and -1 is its "not found"
        // sentinel, so these must not be GLuint.
        GLint locViewProj_ = -1;
        GLint locModel_ = -1;
        GLint locTint_ = -1;
        GLint locLightingEnabled_ = -1;
        GLint locTransparent2D_ = -1;
        bool transparent2D_{};

        GLint locNormalMatrix_ = -1;
        GLint locCameraPos_ = -1;
        GLint locLightDir_ = -1;
        GLint locLightColor_ = -1;
        GLint locLightIntensity_ = -1;
        GLint locMetallic_ = -1;
        GLint locRoughness_ = -1;
        GLint locAo_ = -1;
        GLint locHasNormalMap_ = -1;

        /*!
            @brief
                Looks up u_viewProj, u_model and u_tint in the current program, logging missing ones.
            */
        void cacheUniformLocations();

        /*!
            @brief
                Binds a texture to the albedo unit.
            @param handle
                Texture to bind
            */
        void bindAlbedo(texture::TextureHandle handle);
        void bindNormal(texture::TextureHandle handle);
    };



    class Buffer {
    public:
        /*!
            @brief
                Generates one VAO, one VBO and one EBO.
            */
        Buffer();

        /*!
            @brief
                Deletes the VAO, VBO and EBO.
            */
        ~Buffer();

        // NO COPY
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        // YES MOVE
        /*!
            @brief
                Takes over another buffer's GL objects, leaving it empty.
            @param other
                Buffer to move from
            */
        Buffer(Buffer&& other) noexcept;

        /*!
            @brief
                Deletes this buffer's GL objects and takes over another's.
            @param other
                Buffer to move from
            @return
                This buffer
            */
        Buffer& operator=(Buffer&& other) noexcept;

        /*!
            @brief
                Binds the vertex array.
            */
        void BindVAO() const;

        /*!
            @brief
                Binds the vertex buffer to GL_ARRAY_BUFFER.
            */
        void BindVBO() const;

        /*!
            @brief
                Binds the index buffer to GL_ELEMENT_ARRAY_BUFFER.
            */
        void BindEBO() const;

        /*!
            @brief
                Unbinds any vertex array.
            */
        void UnbindVAO() const;

        /*!
            @brief
                Unbinds GL_ARRAY_BUFFER.
            */
        void UnbindVBO() const;

        /*!
            @brief
                Uploads vertex data to the bound GL_ARRAY_BUFFER.
            @param size
                Byte count
            @param vertices
                Source data, or nullptr to only allocate
            @param usage
                GL usage hint
            */
        void SetData(
            GLsizeiptr size,
            const void* vertices,
            GLenum usage
        ) const;

        /*!
            @brief
                Uploads index data to the bound GL_ELEMENT_ARRAY_BUFFER.
            @param size
                Byte count
            @param data
                Source data
            @param usage
                GL usage hint
            */
        void SetIndexData(
            GLsizeiptr size,
            const void* data,
            GLenum usage
        ) const;

        /*!
            @brief
                Describes and enables one vertex attribute of the bound VAO/VBO.
            @param index
                Attribute location
            @param size
                Component count
            @param type
                Component type
            @param normalized
                Whether integer data is normalised
            @param stride
                Bytes between vertices
            @param offset
                Byte offset of the attribute within a vertex
            */
        void SetAttribute(
            GLuint index,
            GLint size,
            GLenum type,
            GLboolean normalized,
            GLsizei stride,
            const void* offset
        ) const;

        /*!
            @brief
                Whether the VAO and VBO exist.
            @return
                true if both GL objects are nonzero
            */
        bool isValid() const { return mVAO != 0 && mVBO != 0; }

    private:
        GLuint mVAO = 0;
        GLuint mVBO = 0;
        GLuint mEBO = 0;
    };
}


struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texcoord;
    glm::vec3 tangent;
};
