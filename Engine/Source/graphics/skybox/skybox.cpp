#include "skybox.hpp"

// Engine
#include "../shaders/shader.hpp"
#include "../camera/camera.hpp"
#include "../../resources/ResourceManager.hpp"
#include "../texture/texture_types.hpp"
#include "../texture/texture.hpp"

// GLM
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// stds
#include <array>
#include <iostream>
#include <stdexcept>

namespace graphics::skybox
{
    namespace
    {
        // 8 corners of a unit cube, wound CCW when viewed from outside.
        // For a skybox we render the inside, so face culling is disabled.
        constexpr std::array<float, 24> kCubePositions = {
            -0.5f, -0.5f,  0.5f,   // 0
             0.5f, -0.5f,  0.5f,   // 1
             0.5f,  0.5f,  0.5f,   // 2
            -0.5f,  0.5f,  0.5f,   // 3
            -0.5f, -0.5f, -0.5f,  // 4
             0.5f, -0.5f, -0.5f,  // 5
             0.5f,  0.5f, -0.5f,  // 6
            -0.5f,  0.5f, -0.5f,  // 7
        };

        // 12 triangles (36 indices), CCW from outside.
        constexpr std::array<unsigned, 36> kCubeIndices = {
            // Front (+Z)
            0, 1, 2,   0, 2, 3,
            // Back (-Z)
            5, 4, 7,   5, 7, 6,
            // Right (+X)
            1, 5, 6,   1, 6, 2,
            // Left (-X)
            4, 0, 3,   4, 3, 7,
            // Top (+Y)
            3, 2, 6,   3, 6, 7,
            // Bottom (-Y)
            4, 5, 1,   4, 1, 0,
        };
    }

    Skybox::~Skybox()
    {
        shutdown();
    }

    void Skybox::init(vox::ResourceManager& resources, const std::string faces[6])
    {
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        shaderProgram_ = LoadShaders(
            "Engine/Source/graphics/shaders/Skybox.vert",
            "Engine/Source/graphics/shaders/Skybox.frag");

        if (shaderProgram_ == 0)
            throw std::runtime_error("Failed to load skybox shaders.");

        locProjection_ = glGetUniformLocation(shaderProgram_, "u_projection");
        locView_ = glGetUniformLocation(shaderProgram_, "u_view");

        loadCubemap(resources, faces);
        buildCube();
  
    }

    void Skybox::loadCubemap(vox::ResourceManager& resources, const std::string faces[6])
    {
        glGenTextures(1, &cubemap_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_);

        for (int i = 0; i < 6; ++i)
        {
            auto res = resources.load(faces[i]);

            if (!res || res->kind != vox::ResourceKind::Texture)
                throw std::runtime_error("Skybox face not a texture: " + faces[i]);

            if (res->width != res->height)
                std::cerr << "[Skybox] face " << i << " is not square ("
                << res->width << "x" << res->height << ")\n";

            if (vox::isBlockCompressed(res->format))
            {
                // BCn compressed: use glCompressedTexImage2D per face.
                const GLenum internalFormat =
                    graphics::texture::Texture2D::internalFormatFor(
                        res->format, graphics::texture::ColorSpace::Linear);

                const auto& mip = res->mips[0];
                glCompressedTexImage2D(
                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
                    internalFormat, res->width, res->height, 0,
                    static_cast<GLsizei>(mip.size),
                    res->bytes.data() + mip.offset);
            }
            else
            {
                // Uncompressed RGBA8.
                glTexImage2D(
                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
                    GL_RGBA8, res->width, res->height, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    res->bytes.data());
            }
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void Skybox::buildCube()
    {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ibo_);

        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
            kCubePositions.size() * sizeof(float),
            kCubePositions.data(), GL_STATIC_DRAW);

        // Only position: location 0, 3 floats per vertex.
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            kCubeIndices.size() * sizeof(unsigned),
            kCubeIndices.data(), GL_STATIC_DRAW);

        glBindVertexArray(0);
    }

    void Skybox::render(const camera::Camera& cam)
    {
        if (shaderProgram_ == 0 || cubemap_ == 0)
            return;

        glUseProgram(shaderProgram_);

        // View matrix with translation removed: mat4(mat3(view)) zeroes
        // the translation column, so the cube stays centered on the camera.
        glm::mat4 view = glm::mat4(glm::mat3(cam.view()));

        if (locProjection_ >= 0)
            glUniformMatrix4fv(locProjection_, 1, GL_FALSE,
                glm::value_ptr(cam.projection()));

        if (locView_ >= 0)
            glUniformMatrix4fv(locView_, 1, GL_FALSE, glm::value_ptr(view));

        // Bind cubemap to unit 1 (TextureUnit::Skybox).
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(texture::TextureUnit::Skybox));
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_);

        // The skybox cube is viewed from the inside; disable culling so
        // both faces render. Depth test is off so the z=w trick is not
        // even needed, but the shader uses it anyway for safety.
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);

        glBindVertexArray(vao_);
        glDrawElements(GL_TRIANGLES,
            static_cast<GLsizei>(kCubeIndices.size()),
            GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        glUseProgram(0);
    }

    void Skybox::shutdown()
    {
        if (cubemap_) { glDeleteTextures(1, &cubemap_);   cubemap_ = 0; }
        if (vao_) { glDeleteVertexArrays(1, &vao_);  vao_ = 0; }
        if (vbo_) { glDeleteBuffers(1, &vbo_);        vbo_ = 0; }
        if (ibo_) { glDeleteBuffers(1, &ibo_);        ibo_ = 0; }
        if (shaderProgram_) { glDeleteProgram(shaderProgram_); shaderProgram_ = 0; }
    }
}