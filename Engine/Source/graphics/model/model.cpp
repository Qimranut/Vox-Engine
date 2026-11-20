#include "model.hpp"
#include "../../ecs/ecs_manager/ecs_mng.hpp"
#include <glm/glm.hpp>

// stds
#include <cassert>
#include <cstddef>
#include <iostream>

namespace graphics::model
{
    Model& ModelManager::getModel(const std::string& name)
    {
        if (models_.find(name) == models_.end()) {
            std::cout << "Model not found: " << name << std::endl;
            // Return empty model or throw exception
            static Model emptyModel;
            return emptyModel;
        }
        return models_[name];
    }

    bool ModelManager::hasModel(const std::string& name) const
    {
        return models_.find(name) != models_.end();
    }

    void ModelManager::buildCubeModel() { buildPrimitive(false); }

    void ModelManager::buildQuadModel() { buildPrimitive(true); }

    void ModelManager::buildPrimitive(bool flat)
    {
        const glm::vec3 r{ 1,0,0 };
        const glm::vec3 g{ 0,1,0 };
        const glm::vec3 b{ 0,0,1 };

        // UVs use the top-left origin every texture uses (v = 0 is the image's
        // first row). Seen from outside each face, u runs left to right and v
        // top to bottom: side faces have the image's top toward +Y, the top
        // face toward -Z, the bottom face toward +Z. An asymmetric image such
        // as an "F" reads upright and unmirrored on all six.
        std::vector<Vertex> cubeVertices =
        {
            // Front (+Z)  tangent: +X
            {{-0.5f,-0.5f, 0.5f}, {0,0,1}, r, {0.0f,1.0f}, {1,0,0}},
            {{ 0.5f,-0.5f, 0.5f}, {0,0,1}, r, {1.0f,1.0f}, {1,0,0}},
            {{ 0.5f, 0.5f, 0.5f}, {0,0,1}, r, {1.0f,0.0f}, {1,0,0}},
            {{-0.5f, 0.5f, 0.5f}, {0,0,1}, r, {0.0f,0.0f}, {1,0,0}},

            // Back (-Z)  tangent: -X
            {{-0.5f,-0.5f,-0.5f}, {0,0,-1}, r, {1.0f,1.0f}, {-1,0,0}},
            {{ 0.5f,-0.5f,-0.5f}, {0,0,-1}, r, {0.0f,1.0f}, {-1,0,0}},
            {{ 0.5f, 0.5f,-0.5f}, {0,0,-1}, r, {0.0f,0.0f}, {-1,0,0}},
            {{-0.5f, 0.5f,-0.5f}, {0,0,-1}, r, {1.0f,0.0f}, {-1,0,0}},

            // Right (+X)  tangent: -Z
            {{ 0.5f,-0.5f,-0.5f}, {1,0,0}, g, {1.0f,1.0f}, {0,0,-1}},
            {{ 0.5f,-0.5f, 0.5f}, {1,0,0}, g, {0.0f,1.0f}, {0,0,-1}},
            {{ 0.5f, 0.5f, 0.5f}, {1,0,0}, g, {0.0f,0.0f}, {0,0,-1}},
            {{ 0.5f, 0.5f,-0.5f}, {1,0,0}, g, {1.0f,0.0f}, {0,0,-1}},

            // Left (-X)  tangent: +Z
            {{-0.5f,-0.5f,-0.5f}, {-1,0,0}, g, {0.0f,1.0f}, {0,0,1}},
            {{-0.5f, 0.5f,-0.5f}, {-1,0,0}, g, {0.0f,0.0f}, {0,0,1}},
            {{-0.5f, 0.5f, 0.5f}, {-1,0,0}, g, {1.0f,0.0f}, {0,0,1}},
            {{-0.5f,-0.5f, 0.5f}, {-1,0,0}, g, {1.0f,1.0f}, {0,0,1}},

            // Top (+Y)  tangent: +X
            {{-0.5f, 0.5f,-0.5f}, {0,1,0}, b, {0.0f,0.0f}, {1,0,0}},
            {{ 0.5f, 0.5f,-0.5f}, {0,1,0}, b, {1.0f,0.0f}, {1,0,0}},
            {{ 0.5f, 0.5f, 0.5f}, {0,1,0}, b, {1.0f,1.0f}, {1,0,0}},
            {{-0.5f, 0.5f, 0.5f}, {0,1,0}, b, {0.0f,1.0f}, {1,0,0}},

            // Bottom (-Y)  tangent: +X
            {{-0.5f,-0.5f,-0.5f}, {0,-1,0}, b, {0.0f,1.0f}, {1,0,0}},
            {{-0.5f,-0.5f, 0.5f}, {0,-1,0}, b, {0.0f,0.0f}, {1,0,0}},
            {{ 0.5f,-0.5f, 0.5f}, {0,-1,0}, b, {1.0f,0.0f}, {1,0,0}},
            {{ 0.5f,-0.5f,-0.5f}, {0,-1,0}, b, {1.0f,1.0f}, {1,0,0}},
        };

        std::vector<unsigned int> cubeIndices =
        {
            // Front
            0, 1, 2,
            0, 2, 3,

            // Back
            4, 6, 5,
            4, 7, 6,

            // Right
            8, 10, 9,
            8, 11, 10,

            // Left
            12, 14, 13,
            12, 15, 14,

            // Top
            16, 18, 17,
            16, 19, 18,

            // Bottom
            20, 22, 21,
            20, 23, 22
        };

        if (flat) {
            cubeVertices.resize(4);
            for (auto& vertex : cubeVertices) vertex.position.z = 0.f;
            cubeIndices = {0, 1, 2, 0, 2, 3};
        }

        // Back-face culling is on, so every triangle must wind CCW when seen
        // from the side its normal points to. A face wound the other way is
        // culled from outside and drawn from inside, turning the box inside out.
        for (std::size_t i = 0; i + 2 < cubeIndices.size(); i += 3)
        {
            const Vertex& v0 = cubeVertices[cubeIndices[i]];
            const Vertex& v1 = cubeVertices[cubeIndices[i + 1]];
            const Vertex& v2 = cubeVertices[cubeIndices[i + 2]];

            const glm::vec3 faceNormal = glm::cross(v1.position - v0.position, v2.position - v0.position);

            if (glm::dot(faceNormal, v0.normal) <= 0.f)
                std::cerr << "[ModelManager] cube triangle " << i / 3 << " is wound inward\n";

            assert(glm::dot(faceNormal, v0.normal) > 0.f && "cube triangle wound against its normal");
        }



        // Create mesh
        graphics::mesh::Mesh mesh;
        mesh.buffer = std::make_unique<renderer::Buffer>();

        mesh.buffer->BindVAO();
        mesh.buffer->BindVBO();

        mesh.buffer->SetData(
            cubeVertices.size() * sizeof(Vertex),
            cubeVertices.data(),
            GL_STATIC_DRAW);

        mesh.buffer->SetAttribute(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(Vertex),
            (void*)offsetof(Vertex, position));

        mesh.buffer->SetAttribute(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(Vertex),
            (void*)offsetof(Vertex, normal));

        mesh.buffer->SetAttribute(
            2, 3, GL_FLOAT, GL_FALSE,
            sizeof(Vertex),
            (void*)offsetof(Vertex, color));

        mesh.buffer->SetAttribute(
            3, 2, GL_FLOAT, GL_FALSE,
            sizeof(Vertex),
            (void*)offsetof(Vertex, texcoord));

        mesh.buffer->SetAttribute(
            4, 3, GL_FLOAT, GL_FALSE,
            sizeof(Vertex),
            (void*)offsetof(Vertex, tangent));

        mesh.buffer->BindEBO();

        mesh.buffer->SetIndexData(
            cubeIndices.size() * sizeof(unsigned int),
            cubeIndices.data(),
            GL_STATIC_DRAW);

        mesh.buffer->UnbindVBO();
        mesh.buffer->UnbindVAO();

        mesh.vertexCount = static_cast<GLuint>(cubeVertices.size());
        mesh.indexCount = static_cast<GLuint>(cubeIndices.size());
        mesh.hasTexture = true; // UVs cover every face; the texture comes from the entity's RenderMaterial

        Model model;
        model.meshes.push_back(std::move(mesh));
        graphics::lighting::Material material{};
		model.material = material;
        models_[flat ? "menuQuad" : "cube"] = std::move(model);
        std::cout << (flat ? "Menu quad built successfully" : "Cube model built successfully") << std::endl;
    }

}
