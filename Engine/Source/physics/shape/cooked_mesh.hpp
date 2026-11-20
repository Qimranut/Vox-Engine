/*************************************************************************************************************
\file       cooked_mesh.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Jolt-free handle to a cooked static triangle mesh shape, and cookStaticMesh, which builds one
            from an indexed triangle list on any thread.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>

// stds
#include <cstdint>
#include <memory>
#include <vector>


namespace JPH { class Shape; }

namespace physics
{
	/**
	 * A static triangle mesh shape, cooked once and shared by reference.
	 * Jolt-free header: callers outside physics only ever hold the
	 * shared_ptr and hand it to physics::System::addStaticMesh.
	 *
	 */
	class CookedMesh
	{
	public:

		/*!
			@brief
				Releases the reference held on the Jolt shape.
			*/
		~CookedMesh();

		CookedMesh(const CookedMesh&) = delete;
		CookedMesh& operator=(const CookedMesh&) = delete;

		/*!
			@brief
				Number of triangles in the mesh.
			@return
				The triangle count
			*/
		std::uint32_t triangleCount() const { return triangles_; }

		/*!
			@brief
				The Jolt shape. Physics-internal.
			@return
				The shape, or null
			*/
		const JPH::Shape* shape() const { return shape_; }

	private:

		friend std::shared_ptr<const CookedMesh> cookStaticMesh(const std::vector<glm::vec3>&, const std::vector<std::uint32_t>&);

		/*!
			@brief
				Wraps a Jolt shape, taking one reference on it.
			@param shape
				Cooked Jolt shape, may be null
			@param triangles
				Number of triangles in the shape
			*/
		CookedMesh(const JPH::Shape* shape, std::uint32_t triangles);

		const JPH::Shape*	shape_		{ nullptr };	// holds one reference
		std::uint32_t		triangles_	{ 0 };
	};

	/*!
		@brief
			Builds a Jolt MeshShape from an indexed triangle list.

			Pure and thread-safe after physics::System::init(), so voxel meshing
			jobs cook their collision on worker threads.
		@param vertices
			Positions local to the body
		@param indices
			Three per triangle, wound counter-clockwise seen from outside
		@return
			The shape, or nullptr (logged) when Jolt rejects the mesh
		*/
	std::shared_ptr<const CookedMesh> cookStaticMesh(const std::vector<glm::vec3>& vertices, const std::vector<std::uint32_t>& indices);
}
