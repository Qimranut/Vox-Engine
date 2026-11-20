#pragma once
#include "../renderer/renderer.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "../lighting/lighting.hpp"

namespace graphics::mesh
{
    class Mesh
    {
    public:
        std::unique_ptr<renderer::Buffer> buffer;
        GLuint vertexCount = 0;
        GLuint indexCount = 0;
        bool hasTexture = false;

    };
}

namespace graphics::model
{
    class Model
    {
    public:
        std::vector<mesh::Mesh> meshes;
		graphics::lighting::Material material;
    };

    class ModelManager
    {
    public:
        Model& getModel(const std::string& name);
        bool hasModel(const std::string& name) const;

        void buildCubeModel();
        void buildQuadModel();

    private:
        void buildPrimitive(bool flat);
        std::unordered_map<std::string, Model> models_;
    };
}