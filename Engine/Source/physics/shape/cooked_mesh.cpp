/*************************************************************************************************************
\file       cooked_mesh.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements CookedMesh reference handling and cookStaticMesh, which validates an indexed
            triangle list and cooks it into a Jolt MeshShape.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "cooked_mesh.hpp"

// Physics
#include "../math/jolt_glm.hpp"

// Jolt
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <iostream>


namespace physics
{
	/*!
		@brief
			Wraps a Jolt shape, taking one reference on it.
		@param shape
			Cooked Jolt shape, may be null
		@param triangles
			Number of triangles in the shape
		*/
	CookedMesh::CookedMesh(const JPH::Shape* shape, std::uint32_t triangles)
		:
		shape_{ shape },
		triangles_{ triangles }
	{
		if (shape_)
			shape_->AddRef();
	}

	/*!
		@brief
			Releases the reference held on the Jolt shape.
		*/
	CookedMesh::~CookedMesh()
	{
		if (shape_)
			shape_->Release();
	}

	/*!
		@brief
			Builds a Jolt MeshShape from an indexed triangle list.
		@param vertices
			Positions local to the body
		@param indices
			Three per triangle, wound counter-clockwise seen from outside
		@return
			The shape, or nullptr when the list is malformed or Jolt rejects it
		*/
	std::shared_ptr<const CookedMesh> cookStaticMesh(const std::vector<glm::vec3>& vertices, const std::vector<std::uint32_t>& indices)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kPhysics);

		if (indices.size() < 3 || indices.size() % 3 != 0)
			return nullptr;

		JPH::VertexList verts;
		verts.reserve(vertices.size());
		for (const glm::vec3& v : vertices)
			verts.push_back(JPH::Float3(v.x, v.y, v.z));

		JPH::IndexedTriangleList tris;
		tris.reserve(indices.size() / 3);
		for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() || indices[i + 2] >= vertices.size())
				return nullptr;
			tris.emplace_back(indices[i], indices[i + 1], indices[i + 2]);
		}

		JPH::MeshShapeSettings settings(std::move(verts), std::move(tris));
		settings.mPerTriangleUserData = false;

		const JPH::ShapeSettings::ShapeResult result = settings.Create();
		if (result.HasError())
		{
			std::cerr << "[physics] mesh shape creation failed: " << result.GetError().c_str() << "\n";
			return nullptr;
		}

		return std::shared_ptr<const CookedMesh>(new CookedMesh(result.Get().GetPtr(), static_cast<std::uint32_t>(indices.size() / 3)));
	}
}
