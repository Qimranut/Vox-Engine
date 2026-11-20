#pragma once
 
#include <GL/glew.h>
 
namespace graphics::ibl
{
    /**
     * Image-based lighting generated at runtime from the skybox cubemap.
     *
     * Produces three textures:
     *   - Irradiance cubemap (diffuse ambient)
     *   - Prefiltered environment cubemap (specular reflections)
     *   - BRDF LUT (2D, scene-independent)
     *
     * Generated once at startup after the skybox loads. Bound each frame
     * before the PBR shader draws.
     *
     */
    class IBL
    {
    public:
        IBL() = default;
        ~IBL();
 
        // NO COPY
        IBL(const IBL&) = delete;
        IBL& operator=(const IBL&) = delete;
 
        /**
         * Generates all three IBL textures from the skybox cubemap.
         *
         * [param] skyboxCubemap
         * GL_TEXTURE_CUBE_MAP ID of the loaded skybox
         *
         */
        void init(GLuint skyboxCubemap);
 
        /**
         * Binds the three IBL textures to their units. Call before PBR draws.
         *
         */
        void bind() const;
 
        void shutdown();
 
        GLuint irradianceMap()  const { return irradianceMap_; }
        GLuint prefilteredMap() const { return prefilteredMap_; }
        GLuint brdfLut()        const { return brdfLut_; }
 
    private:
        GLuint irradianceMap_   = 0;
        GLuint prefilteredMap_  = 0;
        GLuint brdfLut_         = 0;

        GLuint irradianceFbo_ = 0;
        GLuint irradianceShader_ = 0;
        GLuint cubeVao_ = 0;  // shared with prefilter pass
        GLuint cubeVbo_ = 0;
        GLuint cubeIbo_ = 0;
 
        GLuint brdfLutFbo_      = 0;
        GLuint brdfLutShader_  = 0;

        GLuint prefilteredFbo_ = 0;
        GLuint prefilterShader_ = 0;
 
        void generateBrdfLut(int size);
        void generateIrradiance(GLuint skybox, int size);
        void generatePrefiltered(GLuint skybox, int size);
        void buildCubeMesh();

        GLuint dummyVao_ = 0;
    };
}