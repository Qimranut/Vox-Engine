#include "ibl.hpp"
 
#include "../shaders/shader.hpp"
#include "../texture/texture_types.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
 
#include <iostream>
#include <stdexcept>
 
namespace graphics::ibl
{
    IBL::~IBL()
    {
        shutdown();
    }
 
    void IBL::init(GLuint skyboxCubemap)
    { 
        buildCubeMesh();
        generateBrdfLut(256);
        generateIrradiance(skyboxCubemap, 32);
        generatePrefiltered(skyboxCubemap, 128);
    }
 
    void IBL::generateBrdfLut(int size)
    {
        // --- Load shader ---
        brdfLutShader_ = LoadShaders(
            "Engine/Source/graphics/shaders/IBL/BrdfLut.vert",
            "Engine/Source/graphics/shaders/IBL/BrdfLut.frag");
 
        if (brdfLutShader_ == 0)
            throw std::runtime_error("Failed to load BRDF LUT shaders.");
 
        // --- Create 2D texture ---
        glGenTextures(1, &brdfLut_);
        glBindTexture(GL_TEXTURE_2D, brdfLut_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, size, size, 0, GL_RG, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
 
        // --- Create FBO ---
        glGenFramebuffers(1, &brdfLutFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, brdfLutFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, brdfLut_, 0);
 
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("BRDF LUT FBO incomplete.");

        glGenVertexArrays(1, &dummyVao_);
 
        // --- Render ---
        glViewport(0, 0, size, size);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);

        glUseProgram(brdfLutShader_);
        glBindFramebuffer(GL_FRAMEBUFFER, brdfLutFbo_);
        glBindVertexArray(dummyVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
 
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glUseProgram(0);
 
        std::cout << "[IBL] BRDF LUT generated (" << size << "x" << size << ")\n";
    }

    void IBL::generateIrradiance(GLuint skybox, int size)
    {
        irradianceShader_ = LoadShaders(
            "Engine/Source/graphics/shaders/IBL/Irradiance.vert",
            "Engine/Source/graphics/shaders/IBL/Irradiance.frag");

        if (irradianceShader_ == 0)
            throw std::runtime_error("Failed to load irradiance shaders.");

        // Create irradiance cubemap.
        glGenTextures(1, &irradianceMap_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap_);
        for (int i = 0; i < 6; ++i)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                size, size, 0, GL_RGB, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        // Create FBO.
        glGenFramebuffers(1, &irradianceFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, irradianceFbo_);

        // Bind skybox to unit 1 for sampling.
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(texture::TextureUnit::Skybox));
        glBindTexture(GL_TEXTURE_CUBE_MAP, skybox);

        // 6 view matrices, one per face. Projection is 90 degree FOV.
        glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        glm::mat4 captureViews[] = {
            glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        };

        GLint locProj = glGetUniformLocation(irradianceShader_, "u_projection");
        GLint locView = glGetUniformLocation(irradianceShader_, "u_view");

        glViewport(0, 0, size, size);
        glBindFramebuffer(GL_FRAMEBUFFER, irradianceFbo_);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(irradianceShader_);

        if (locProj >= 0)
            glUniformMatrix4fv(locProj, 1, GL_FALSE, glm::value_ptr(captureProjection));

        for (int face = 0; face < 6; ++face)
        {
            if (locView >= 0)
                glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(captureViews[face]));

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, irradianceMap_, 0);

            glBindVertexArray(cubeVao_);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glUseProgram(0);

        std::cout << "[IBL] irradiance cubemap generated (" << size << "x" << size << ")\n";
    }

    void IBL::generatePrefiltered(GLuint skybox, int size)
    {
        prefilterShader_ = LoadShaders(
            "Engine/Source/graphics/shaders/IBL/Prefilter.vert",
            "Engine/Source/graphics/shaders/IBL/Prefilter.frag");

        if (prefilterShader_ == 0)
            throw std::runtime_error("Failed to load prefilter shaders.");

        const int maxMip = 5;

        // Create prefiltered cubemap with mip chain.
        glGenTextures(1, &prefilteredMap_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, prefilteredMap_);
        for (int i = 0; i < 6; ++i)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                size, size, 0, GL_RGB, GL_HALF_FLOAT, nullptr);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        // Create FBO.
        glGenFramebuffers(1, &prefilteredFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, prefilteredFbo_);

        // Bind skybox to unit 1 for sampling.
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(texture::TextureUnit::Skybox));
        glBindTexture(GL_TEXTURE_CUBE_MAP, skybox);

        // 6 view matrices, same as irradiance.
        glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        glm::mat4 captureViews[] = {
            glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        };

        GLint locProj = glGetUniformLocation(prefilterShader_, "u_projection");
        GLint locView = glGetUniformLocation(prefilterShader_, "u_view");
        GLint locRoughness = glGetUniformLocation(prefilterShader_, "u_roughness");

        glUseProgram(prefilterShader_);
        if (locProj >= 0)
            glUniformMatrix4fv(locProj, 1, GL_FALSE, glm::value_ptr(captureProjection));

        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);

        for (int mip = 0; mip < maxMip; ++mip)
        {
            const int mipSize = size >> mip;
            const float roughness = static_cast<float>(mip) / static_cast<float>(maxMip - 1);

            glViewport(0, 0, mipSize, mipSize);

            if (locRoughness >= 0)
                glUniform1f(locRoughness, roughness);

            for (int face = 0; face < 6; ++face)
            {
                if (locView >= 0)
                    glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(captureViews[face]));

                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, prefilteredMap_, mip);

                glBindVertexArray(cubeVao_);
                glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
                glBindVertexArray(0);
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glUseProgram(0);

        std::cout << "[IBL] prefiltered cubemap generated (" << size << "x" << size
            << ", " << maxMip << " mips)\n";
    }


    void IBL::buildCubeMesh()
    {
        constexpr float positions[] = {
            -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
            -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,
        };
        constexpr unsigned indices[] = {
            0, 1, 2, 0, 2, 3,  5, 4, 7, 5, 7, 6,
            1, 5, 6, 1, 6, 2,  4, 0, 3, 4, 3, 7,
            3, 2, 6, 3, 6, 7,  4, 5, 1, 4, 1, 0,
        };

        glGenVertexArrays(1, &cubeVao_);
        glGenBuffers(1, &cubeVbo_);
        glGenBuffers(1, &cubeIbo_);

        glBindVertexArray(cubeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeIbo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
        glBindVertexArray(0);
    }

 
    void IBL::bind() const
    {
        if (irradianceMap_)
        {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(texture::TextureUnit::Irradiance));
            glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap_);
        }
        if (prefilteredMap_)
        {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(texture::TextureUnit::Prefiltered));
            glBindTexture(GL_TEXTURE_CUBE_MAP, prefilteredMap_);
        }
        if (brdfLut_)
        {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(texture::TextureUnit::BrdfLut));
            glBindTexture(GL_TEXTURE_2D, brdfLut_);
        }
    }
 
    void IBL::shutdown()
    {
        if (prefilteredMap_) { glDeleteTextures(1, &prefilteredMap_);  prefilteredMap_ = 0; }
        if (prefilteredFbo_) { glDeleteFramebuffers(1, &prefilteredFbo_); prefilteredFbo_ = 0; }
        if (prefilterShader_) { glDeleteProgram(prefilterShader_);       prefilterShader_ = 0; }
        if (irradianceMap_) { glDeleteTextures(1, &irradianceMap_);   irradianceMap_ = 0; }
        if (irradianceFbo_) { glDeleteFramebuffers(1, &irradianceFbo_); irradianceFbo_ = 0; }
        if (irradianceShader_) { glDeleteProgram(irradianceShader_);     irradianceShader_ = 0; }
        if (brdfLut_)       { glDeleteTextures(1, &brdfLut_);       brdfLut_ = 0; }
        if (brdfLutFbo_)    { glDeleteFramebuffers(1, &brdfLutFbo_); brdfLutFbo_ = 0; }
        if (brdfLutShader_) { glDeleteProgram(brdfLutShader_);       brdfLutShader_ = 0; }
        if (dummyVao_) { glDeleteVertexArrays(1, &dummyVao_); dummyVao_ = 0; }
        if (cubeVao_) { glDeleteVertexArrays(1, &cubeVao_);     cubeVao_ = 0; }
        if (cubeVbo_) { glDeleteBuffers(1, &cubeVbo_);          cubeVbo_ = 0; }
        if (cubeIbo_) { glDeleteBuffers(1, &cubeIbo_);          cubeIbo_ = 0; }
    }
}