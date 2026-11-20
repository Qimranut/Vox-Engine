#pragma once

#include <glm/glm.hpp>

namespace graphics::lighting
{
    class PointLight
    {
    public:
        PointLight() = default;

        PointLight(const glm::vec3& position,
            const glm::vec3& color = glm::vec3(1.0f),
            float intensity = 1.0f)
            : position_(position)
            , color_(color)
            , intensity_(intensity)
        {
        }

        // Position
        void setPosition(const glm::vec3& position) { position_ = position; }
        const glm::vec3& position() const { return position_; }

        // Color
        void setColor(const glm::vec3& color) { color_ = color; }
        const glm::vec3& color() const { return color_; }

        // Intensity
        void setIntensity(float intensity) { intensity_ = intensity; }
        float intensity() const { return intensity_; }

    private:
        glm::vec3 position_{ 0.0f, 5.0f, 5.0f };  // Default: above and in front
        glm::vec3 color_{ 1.0f, 1.0f, 1.0f };     // Default: white light
        float intensity_{ 1.0f };                   // Default: full brightness
    };

    class Material
    {
    public:
        Material() = default;

        Material(const glm::vec3& albedo,
            float metallic,
            float roughness)
            : albedo_(albedo)
            , metallic_(metallic)
            , roughness_(roughness)
        {
        }

        void setAlbedo(const glm::vec3& albedo) { albedo_ = albedo; }
        const glm::vec3& albedo() const { return albedo_; }

        void setMetallic(float metallic) { metallic_ = metallic; }
        float metallic() const { return metallic_; }

        void setRoughness(float roughness) { roughness_ = roughness; }
        float roughness() const { return roughness_; }

    private:
        glm::vec3 albedo_{ 1.0f, 0.0f, 0.0f };
        float metallic_{ 0.5f };
        float roughness_{ 0.5f };
    };
}