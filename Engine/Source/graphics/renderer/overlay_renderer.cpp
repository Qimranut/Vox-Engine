/*************************************************************************************************************
\file       overlay_renderer.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      OverlayRenderer: builds unlit triangle shapes (boxes, spheres, cones, cylinders, edge strips,
            discs, circles, grids) into Scene and OnTop queues and flushes them with blending and depth
            state that it saves and restores.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "overlay_renderer.hpp"

// Graphics
#include "../camera/camera.hpp"
#include "../shaders/shader.hpp"

// GLM
#include <glm/gtc/type_ptr.hpp>

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <cmath>
#include <cstddef>
#include <iostream>


namespace graphics::renderer
{
	namespace
	{
		constexpr float kTau = 6.28318530718f;

		constexpr int kRound = 16;		// segments around a cone, cylinder or sphere
		constexpr int kStacks = 8;		// sphere rings pole to pole

		// OnTop shapes are drawn into this sliver at the front of the depth
		// range: in front of any scene depth, yet still ordered by depth
		// among themselves.
		constexpr double kOnTopDepthFar = 0.01;

		/*!
			@brief
				Two unit vectors square to w and to each other, with u x v = w.
			@param w
				Unit axis to build the basis around
			@param u
				Receives the first perpendicular unit vector
			@param v
				Receives the second perpendicular unit vector
			*/
		void basisAround(const glm::vec3& w, glm::vec3& u, glm::vec3& v)
		{
			const glm::vec3 helper = std::fabs(w.y) < 0.9f ? glm::vec3{ 0.f, 1.f, 0.f } : glm::vec3{ 1.f, 0.f, 0.f };
			u = glm::normalize(glm::cross(helper, w));
			v = glm::cross(w, u);
		}
	}

	/*!
		@brief
			Builds the overlay shader and the VAO/VBO with the Vertex layout; leaves the renderer invalid if the shader fails.
		*/
	OverlayRenderer::OverlayRenderer()
		: buffer_{ std::make_unique<Buffer>() }
	{
		program_ = LoadShaders("Engine/Source/graphics/shaders/Overlay.vert", "Engine/Source/graphics/shaders/Overlay.frag");
		if (program_ == 0)
		{
			std::cerr << "[OverlayRenderer] shaders did not build; overlays are off\n";
			return;
		}

		locViewProj_ = glGetUniformLocation(program_, "u_viewProj");
		locToEye_ = glGetUniformLocation(program_, "u_toEye");

		const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));

		buffer_->BindVAO();
		buffer_->BindVBO();
		buffer_->SetData(0, nullptr, GL_DYNAMIC_DRAW);
		buffer_->SetAttribute(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, pos_)));
		buffer_->SetAttribute(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, normal_)));
		buffer_->SetAttribute(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, colour_)));
		buffer_->UnbindVAO();
		buffer_->UnbindVBO();
	}

	/*!
		@brief
			Deletes the shader program.
		*/
	OverlayRenderer::~OverlayRenderer()
	{
		if (program_ != 0)
			glDeleteProgram(program_);
	}

	/*!
		@brief
			Drops every queued shape and resets the transform.
		*/
	void OverlayRenderer::clear()
	{
		scene_.clear();
		onTop_.clear();

		// A transform left set by one frame must not bend the next one's shapes.
		resetTransform();
	}

	/*!
		@brief
			Appends one triangle to a queue, applying the local-to-world transform if one is set.
		@param out
			Queue to append to
		@param a
			First corner
		@param b
			Second corner
		@param c
			Third corner
		@param na
			Normal at a (zero for flat, unshaded)
		@param nb
			Normal at b
		@param nc
			Normal at c
		@param colour
			Straight (non-premultiplied) RGBA
		*/
	void OverlayRenderer::triangle(std::vector<Vertex>& out, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
								   const glm::vec3& na, const glm::vec3& nb, const glm::vec3& nc, const glm::vec4& colour)
	{
		// The one place every shape passes through, so the one place the
		// optional local-to-world transform is applied.
		if (transformed_)
		{
			const glm::mat3 rotate(transform_);
			out.push_back({ glm::vec3(transform_ * glm::vec4(a, 1.f)), rotate * na, colour });
			out.push_back({ glm::vec3(transform_ * glm::vec4(b, 1.f)), rotate * nb, colour });
			out.push_back({ glm::vec3(transform_ * glm::vec4(c, 1.f)), rotate * nc, colour });
			return;
		}

		out.push_back({ a, na, colour });
		out.push_back({ b, nb, colour });
		out.push_back({ c, nc, colour });
	}

	/*!
		@brief
			Sets a local-to-world transform applied to every shape queued after it.
		@param localToWorld
			Transform from shape coordinates to world space
		*/
	void OverlayRenderer::setTransform(const glm::mat4& localToWorld)
	{
		transform_ = localToWorld;
		transformed_ = true;
	}

	/*!
		@brief
			Returns to world-space shapes.
		*/
	void OverlayRenderer::resetTransform()
	{
		transform_ = glm::mat4{ 1.f };
		transformed_ = false;
	}

	// Every shape below is wound counter-clockwise seen from outside, which is
	// the engine's front face: flush() relies on it to draw the far side of a
	// translucent shape before its near side.

	/*!
		@brief
			Queues a solid axis-aligned box as twelve outward-wound, flat-shaded triangles.
		@param layer
			Scene or OnTop
		@param mn
			Minimum corner
		@param mx
			Maximum corner
		@param colour
			Straight RGBA
		*/
	void OverlayRenderer::box(Layer layer, const glm::vec3& mn, const glm::vec3& mx, const glm::vec4& colour)
	{
		if (!valid())
			return;

		std::vector<Vertex>& out = queue(layer);
		const glm::vec3 flat{ 0.f };

		for (int a = 0; a < 3; ++a)
		{
			// b x c = a, so (b-, c-) (b+, c-) (b+, c+) (b-, c+) runs
			// counter-clockwise seen from +a.
			const int b = (a + 1) % 3, c = (a + 2) % 3;

			for (int side = 0; side < 2; ++side)
			{
				glm::vec3 p[4];
				for (int k = 0; k < 4; ++k)
				{
					p[k][a] = side == 1 ? mx[a] : mn[a];
					p[k][b] = (k == 1 || k == 2) ? mx[b] : mn[b];
					p[k][c] = (k >= 2) ? mx[c] : mn[c];
				}

				if (side == 1)
				{
					triangle(out, p[0], p[1], p[2], flat, flat, flat, colour);
					triangle(out, p[0], p[2], p[3], flat, flat, flat, colour);
				}
				else
				{
					triangle(out, p[0], p[2], p[1], flat, flat, flat, colour);
					triangle(out, p[0], p[3], p[2], flat, flat, flat, colour);
				}
			}
		}
	}

	/*!
		@brief
			Queues a shaded UV sphere of kStacks rings by kRound slices.
		@param layer
			Scene or OnTop
		@param centre
			Sphere centre
		@param radius
			Sphere radius
		@param colour
			Straight RGBA
		*/
	void OverlayRenderer::sphere(Layer layer, const glm::vec3& centre, float radius, const glm::vec4& colour)
	{
		if (!valid())
			return;

		std::vector<Vertex>& out = queue(layer);

		// Rings from the south pole up; around each ring z -> x, so that
		// (ring direction) x (up the sphere) points outward.
		const auto normalAt = [](int stack, int slice)
		{
			const float polar = 3.14159265f * static_cast<float>(stack) / static_cast<float>(kStacks);
			const float around = kTau * static_cast<float>(slice) / static_cast<float>(kRound);
			const float ring = std::sin(polar);
			return glm::vec3{ ring * std::sin(around), -std::cos(polar), ring * std::cos(around) };
		};

		for (int stack = 0; stack < kStacks; ++stack)
		for (int slice = 0; slice < kRound; ++slice)
		{
			const glm::vec3 l0 = normalAt(stack, slice), l1 = normalAt(stack, slice + 1);
			const glm::vec3 u0 = normalAt(stack + 1, slice), u1 = normalAt(stack + 1, slice + 1);

			triangle(out, centre + l0 * radius, centre + l1 * radius, centre + u1 * radius, l0, l1, u1, colour);
			triangle(out, centre + l0 * radius, centre + u1 * radius, centre + u0 * radius, l0, u1, u0, colour);
		}
	}

	/*!
		@brief
			Queues a shaded cone with a capped base; does nothing for a zero-length axis.
		@param layer
			Scene or OnTop
		@param base
			Centre of the base disc
		@param tip
			Apex
		@param radius
			Base radius
		@param colour
			Straight RGBA
		*/
	void OverlayRenderer::cone(Layer layer, const glm::vec3& base, const glm::vec3& tip, float radius, const glm::vec4& colour)
	{
		if (!valid())
			return;

		const glm::vec3 axis = tip - base;
		const float length = glm::length(axis);
		if (length < 1e-6f)
			return;

		std::vector<Vertex>& out = queue(layer);
		const glm::vec3 w = axis / length;
		glm::vec3 u, v;
		basisAround(w, u, v);

		// A cone's normal leans toward the tip by the slope of its side.
		const float lean = radius / length;
		const auto radial = [&](int slice)
		{
			const float around = kTau * static_cast<float>(slice) / static_cast<float>(kRound);
			return u * std::cos(around) + v * std::sin(around);
		};

		for (int slice = 0; slice < kRound; ++slice)
		{
			const glm::vec3 r0 = radial(slice), r1 = radial(slice + 1);
			const glm::vec3 n0 = glm::normalize(r0 + w * lean), n1 = glm::normalize(r1 + w * lean);
			const glm::vec3 a0 = base + r0 * radius, a1 = base + r1 * radius;

			triangle(out, a0, a1, tip, n0, n1, glm::normalize(n0 + n1), colour);
			triangle(out, base, a1, a0, -w, -w, -w, colour);
		}
	}

	/*!
		@brief
			Queues a shaded cylinder capped at both ends; does nothing for a zero-length axis.
		@param layer
			Scene or OnTop
		@param from
			Centre of one end
		@param to
			Centre of the other end
		@param radius
			Cylinder radius
		@param colour
			Straight RGBA
		*/
	void OverlayRenderer::cylinder(Layer layer, const glm::vec3& from, const glm::vec3& to, float radius, const glm::vec4& colour)
	{
		if (!valid())
			return;

		const glm::vec3 axis = to - from;
		const float length = glm::length(axis);
		if (length < 1e-6f)
			return;

		std::vector<Vertex>& out = queue(layer);
		const glm::vec3 w = axis / length;
		glm::vec3 u, v;
		basisAround(w, u, v);

		const auto radial = [&](int slice)
		{
			const float around = kTau * static_cast<float>(slice) / static_cast<float>(kRound);
			return u * std::cos(around) + v * std::sin(around);
		};

		for (int slice = 0; slice < kRound; ++slice)
		{
			const glm::vec3 r0 = radial(slice), r1 = radial(slice + 1);
			const glm::vec3 a0 = from + r0 * radius, a1 = from + r1 * radius;
			const glm::vec3 b0 = to + r0 * radius, b1 = to + r1 * radius;

			triangle(out, a0, a1, b1, r0, r1, r1, colour);
			triangle(out, a0, b1, b0, r0, r1, r0, colour);
			triangle(out, to, b0, b1, w, w, w, colour);
			triangle(out, from, a1, a0, -w, -w, -w, colour);
		}
	}

	/*!
		@brief
			Queues a flat, double-sided strip from one point to another, turned to face the eye.

			Does nothing when the segment is seen end on.
		@param layer
			Scene or OnTop
		@param from
			Start point
		@param to
			End point
		@param widthFrom
			World width at from
		@param widthTo
			World width at to
		@param eye
			Camera position the strip faces
		@param colour
			Straight RGBA
		*/
	void OverlayRenderer::edge(Layer layer, const glm::vec3& from, const glm::vec3& to, float widthFrom, float widthTo,
							   const glm::vec3& eye, const glm::vec4& colour)
	{
		if (!valid())
			return;

		const glm::vec3 along = to - from;
		const glm::vec3 across = glm::cross(along, eye - 0.5f * (from + to));
		const float len = glm::length(across);
		if (len < 1e-9f)
			return;		// seen end on: nothing to show

		const glm::vec3 side = across / len;
		const glm::vec3 a0 = from - side * (0.5f * widthFrom), a1 = from + side * (0.5f * widthFrom);
		const glm::vec3 b0 = to - side * (0.5f * widthTo), b1 = to + side * (0.5f * widthTo);
		const glm::vec3 flat{ 0.f };
		std::vector<Vertex>& out = queue(layer);

		// Both windings: a strip has no inside, and must survive either cull.
		triangle(out, a0, b0, b1, flat, flat, flat, colour);
		triangle(out, a0, b1, a1, flat, flat, flat, colour);
		triangle(out, a0, b1, b0, flat, flat, flat, colour);
		triangle(out, a0, a1, b1, flat, flat, flat, colour);
	}

	/*!
		@brief
			Queues the twelve edges of an axis-aligned box as eye-facing strips.
		@param layer
			Scene or OnTop
		@param mn
			Minimum corner
		@param mx
			Maximum corner
		@param width
			World width of every edge
		@param eye
			Camera position the strips face
		@param colour
			Straight RGBA
		*/
	void OverlayRenderer::boxEdges(Layer layer, const glm::vec3& mn, const glm::vec3& mx, float width, const glm::vec3& eye, const glm::vec4& colour)
	{
		const auto corner = [&](int i)
		{
			return glm::vec3{ (i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z };
		};

		// Corners that differ in exactly one bit share an edge.
		for (int i = 0; i < 8; ++i)
		for (int bit = 1; bit <= 4; bit <<= 1)
			if ((i & bit) == 0)
				edge(layer, corner(i), corner(i | bit), width, width, eye, colour);
	}

	/*!
		@brief
			Queues a flat, double-sided filled disc; does nothing for a zero normal.
		@param layer
			Scene or OnTop
		@param centre
			Disc centre
		@param normal
			Disc normal (need not be unit length)
		@param radius
			Disc radius
		@param colour
			Straight RGBA
		*/
	void OverlayRenderer::disc(Layer layer, const glm::vec3& centre, const glm::vec3& normal, float radius, const glm::vec4& colour)
	{
		if (!valid() || glm::length(normal) < 1e-6f)
			return;

		std::vector<Vertex>& out = queue(layer);
		glm::vec3 u, v;
		basisAround(glm::normalize(normal), u, v);
		const glm::vec3 flat{ 0.f };

		const auto rim = [&](int slice)
		{
			const float around = kTau * static_cast<float>(slice) / static_cast<float>(kRound);
			return centre + (u * std::cos(around) + v * std::sin(around)) * radius;
		};

		// Both windings, like edge(): a disc has no inside.
		for (int slice = 0; slice < kRound; ++slice)
		{
			triangle(out, centre, rim(slice), rim(slice + 1), flat, flat, flat, colour);
			triangle(out, centre, rim(slice + 1), rim(slice), flat, flat, flat, colour);
		}
	}

	/*!
		@brief
			Queues a ring outline of 64 eye-facing edge strips; does nothing for a zero normal.
		@param layer
			Scene or OnTop
		@param centre
			Ring centre
		@param normal
			Ring normal (need not be unit length)
		@param radius
			Ring radius
		@param width
			World width of the outline
		@param eye
			Camera position the strips face
		@param colour
			Straight RGBA
		*/
	void OverlayRenderer::circle(Layer layer, const glm::vec3& centre, const glm::vec3& normal, float radius, float width,
								 const glm::vec3& eye, const glm::vec4& colour)
	{
		if (!valid() || glm::length(normal) < 1e-6f)
			return;

		glm::vec3 u, v;
		basisAround(glm::normalize(normal), u, v);

		// Finer than the solids: a large ring seen close up shows its corners.
		constexpr int kSegments = 64;
		const auto at = [&](int i)
		{
			const float around = kTau * static_cast<float>(i) / static_cast<float>(kSegments);
			return centre + (u * std::cos(around) + v * std::sin(around)) * radius;
		};

		for (int i = 0; i < kSegments; ++i)
			edge(layer, at(i), at(i + 1), width, width, eye, colour);
	}

	/*!
		@brief
			Queues a square grid of eye-facing lines in the plane through centre.
		@param layer
			Scene or OnTop
		@param centre
			Grid centre
		@param normal
			Plane normal (need not be unit length)
		@param halfExtent
			Half the side length of the grid
		@param spacing
			Distance between lines; nothing is drawn if not positive
		@param width
			World width of each line
		@param eye
			Camera position the strips face
		@param colour
			Straight RGBA
		*/
	void OverlayRenderer::grid(Layer layer, const glm::vec3& centre, const glm::vec3& normal, float halfExtent, float spacing, float width,
							   const glm::vec3& eye, const glm::vec4& colour)
	{
		if (!valid() || spacing <= 1e-6f || glm::length(normal) < 1e-6f)
			return;

		glm::vec3 u, v;
		basisAround(glm::normalize(normal), u, v);

		const int lines = static_cast<int>(halfExtent / spacing);
		for (int i = -lines; i <= lines; ++i)
		{
			const float at = static_cast<float>(i) * spacing;
			edge(layer, centre + u * at - v * halfExtent, centre + u * at + v * halfExtent, width, width, eye, colour);
			edge(layer, centre + v * at - u * halfExtent, centre + v * at + u * halfExtent, width, width, eye, colour);
		}
	}

	/*!
		@brief
			Uploads one queue into the dynamic buffer and draws it as GL_TRIANGLES.
		@param vertices
			Queued triangle vertices
		*/
	void OverlayRenderer::draw(const std::vector<Vertex>& vertices)
	{
		buffer_->BindVAO();
		buffer_->BindVBO();

		// Full re-specification each time lets the driver orphan the old
		// storage instead of stalling on a buffer the GPU may still be reading.
		buffer_->SetData(static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_DYNAMIC_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

		buffer_->UnbindVAO();
		buffer_->UnbindVBO();
	}

	/*!
		@brief
			Draws and clears everything queued: Scene far faces then near faces without depth writes,
			then OnTop in the front depth sliver.

			Saves and restores every GL state it changes.
		@param camera
			Camera whose view-projection and forward axis are used
		*/
	void OverlayRenderer::flush(const camera::Camera& camera)
	{
		if (!valid() || (scene_.empty() && onTop_.empty()))
		{
			clear();
			return;
		}

		VOX_PROFILE_SCOPE_C("Overlay", profile::colour::kGraphics);

		// What this pass changes, as it found it.
		const GLboolean hadBlend = glIsEnabled(GL_BLEND), hadCull = glIsEnabled(GL_CULL_FACE), hadDepth = glIsEnabled(GL_DEPTH_TEST);
		GLboolean depthMask = GL_TRUE;
		GLint cullMode = GL_BACK, depthFunc = GL_LESS;
		GLint srcRgb = GL_ONE, dstRgb = GL_ZERO, srcAlpha = GL_ONE, dstAlpha = GL_ZERO;
		GLdouble depthRange[2] = { 0.0, 1.0 };
		glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
		glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
		glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
		glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
		glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
		glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
		glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
		glGetDoublev(GL_DEPTH_RANGE, depthRange);

		glUseProgram(program_);
		if (locViewProj_ >= 0)
			glUniformMatrix4fv(locViewProj_, 1, GL_FALSE, glm::value_ptr(camera.viewProjection()));
		if (locToEye_ >= 0)
			glUniform3fv(locToEye_, 1, glm::value_ptr(-camera.forward()));

		glEnable(GL_BLEND);
		// Alpha is kept as the framebuffer had it: the Editor shows this
		// texture through ImGui, which would blend a lowered alpha again.
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glEnable(GL_CULL_FACE);

		if (!scene_.empty())
		{
			// Translucent and convex: far faces, then near faces, without
			// writing depth, so a shape never hides its own back.
			glDepthMask(GL_FALSE);
			glCullFace(GL_FRONT);
			draw(scene_);
			glCullFace(GL_BACK);
			draw(scene_);
		}

		if (!onTop_.empty())
		{
			glDepthMask(GL_TRUE);
			glDepthRange(0.0, kOnTopDepthFar);
			glCullFace(GL_BACK);
			draw(onTop_);
		}

		glUseProgram(0);

		glDepthRange(depthRange[0], depthRange[1]);
		glDepthMask(depthMask);
		glDepthFunc(static_cast<GLenum>(depthFunc));
		glCullFace(static_cast<GLenum>(cullMode));
		glBlendFuncSeparate(static_cast<GLenum>(srcRgb), static_cast<GLenum>(dstRgb), static_cast<GLenum>(srcAlpha), static_cast<GLenum>(dstAlpha));
		if (!hadBlend) glDisable(GL_BLEND);
		if (!hadCull) glDisable(GL_CULL_FACE);
		if (!hadDepth) glDisable(GL_DEPTH_TEST);

		clear();
	}
}
