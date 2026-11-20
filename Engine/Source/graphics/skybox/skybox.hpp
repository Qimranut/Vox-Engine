#pragma once
 
#include <GL/glew.h>
#include <string>
 
namespace vox { class ResourceManager; }
namespace graphics { namespace camera { class Camera; } }
 
namespace graphics::skybox
{
    /**
     * A cubemap skybox rendered as a cube around the camera.
     *
     * Loads 6 separate face textures and assembles them into a
     * GL_TEXTURE_CUBE_MAP. Drawn first each frame with depth test
     * disabled, so it fills the background without affecting the
     * depth buffer.
     *
     */
    class Skybox
    {
    public:
        Skybox() = default;
        ~Skybox();
 
        // NO COPY
        Skybox(const Skybox&) = delete;
        Skybox& operator=(const Skybox&) = delete;
 
        /**
         * Loads 6 face textures, compiles the skybox shader, and
         * builds a cube mesh with positions only.
         *
         * [param] resources
         * Decodes texture files. Must outlive the skybox.
         *
         * [param] faces
         * 6 asset-relative paths in GL order:
         *   0: +X, 1: -X, 2: +Y, 3: -Y, 4: +Z, 5: -Z
         *
         */
        void init(vox::ResourceManager& resources, const std::string faces[6]);

        GLuint cubemapId() const { return cubemap_; }
 
        /**
         * Draws the skybox. Call after clear(), before entity rendering.
         * Uses its own shader program; does not touch the depth buffer.
         *
         */
        void render(const camera::Camera& cam);
 
        void shutdown();
 
    private:
        GLuint shaderProgram_    = 0;
        GLuint cubemap_           = 0;
        GLuint vao_               = 0;
        GLuint vbo_               = 0;
        GLuint ibo_               = 0;
 
        GLint locProjection_      = -1;
        GLint locView_            = -1;
 
        void loadCubemap(vox::ResourceManager& resources, const std::string faces[6]);
        void buildCube();
    };
}