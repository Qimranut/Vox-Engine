/*************************************************************************************************************
\file       camera.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Yaw/pitch camera: transform and projection setters, lazily recomputed and cached view,
            projection and view-projection matrices, and Gribb-Hartmann frustum extraction for culling.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "camera.hpp"

// GLM
#include <glm/gtc/matrix_transform.hpp>

// stds
#include <cmath>


namespace graphics::camera
{
	namespace
	{
		/*!
			@brief
				Extracts a row from a column-major glm matrix.
			@param m
				Matrix to read
			@param i
				Row index
			@return
				The i-th row
			*/
		glm::vec4 row(const glm::mat4& m, int i)
		{
			return { m[0][i], m[1][i], m[2][i], m[3][i] };
		}

		/*!
			@brief
				Normalises a plane equation packed as (n.xyz, d).

				A zero-length normal yields a default +Y plane through the origin.
			@param p
				Unnormalised plane
			@return
				Plane whose signedDistance() is a true distance
			*/
		Plane makePlane(const glm::vec4& p)
		{
			const float len = glm::length(glm::vec3(p));

			if (len <= 0.f)
				return Plane{ { 0.f, 1.f, 0.f }, 0.f };

			return Plane{ glm::vec3(p) / len, p.w / len };
		}
	}

	// ------------------------- Transform -------------------------

	/*!
		@brief
			Moves the camera to a world position and marks the view-dependent caches dirty.
		@param position
			New world position of the eye
		*/
	void Camera::setPosition(const glm::vec3& position)
	{
		position_ = position;
		viewDirty_ = true;
		viewProjDirty_ = true;
		frustumDirty_ = true;
	}

	/*!
		@brief
			Sets the camera orientation and marks the view-dependent caches dirty.
		@param yawRad
			Yaw in radians, measured from -Z, positive turning left
		@param pitchRad
			Pitch in radians, positive looking up (not clamped here)
		*/
	void Camera::setYawPitch(float yawRad, float pitchRad)
	{
		yaw_ = yawRad;
		pitch_ = pitchRad;
		viewDirty_ = true;
		viewProjDirty_ = true;
		frustumDirty_ = true;
	}

	/*!
		@brief
			Places the camera and aims it at a target, decomposing the direction into yaw/pitch.

			Orientation is left unchanged when eye and target coincide.
		@param eye
			World position of the eye
		@param target
			World position to look at
		@param const glm::vec3&
			Up vector, ignored (roll is always discarded)
		*/
	void Camera::lookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3&)
	{
		position_ = eye;

		const glm::vec3 delta = target - eye;
		const float len = glm::length(delta);

		if (len > 0.f)
		{
			const glm::vec3 dir = delta / len;

			pitch_ = std::asin(glm::clamp(dir.y, -1.f, 1.f));
			yaw_ = std::atan2(-dir.x, -dir.z);
		}

		viewDirty_ = true;
		viewProjDirty_ = true;
		frustumDirty_ = true;
	}

	// ------------------------ Projection -------------------------

	/*!
		@brief
			Switches to a perspective projection and sets all of its parameters.
		@param fovYRad
			Vertical field of view, in radians
		@param aspect
			Width / height; ignored if not strictly positive
		@param nearZ
			Near clip distance
		@param farZ
			Far clip distance
		*/
	void Camera::setPerspective(float fovYRad, float aspect, float nearZ, float farZ)
	{
		mode_ = Projection::Perspective;
		fovY_ = fovYRad;
		nearZ_ = nearZ;
		farZ_ = farZ;

		if (aspect > 0.f)
			aspect_ = aspect;

		projDirty_ = true;
		viewProjDirty_ = true;
		frustumDirty_ = true;
	}

	/*!
		@brief
			Switches to an orthographic projection and sets all of its parameters.
		@param halfHeight
			Half the vertical extent of the view volume, in world units
		@param aspect
			Width / height; ignored if not strictly positive
		@param nearZ
			Near clip distance
		@param farZ
			Far clip distance
		*/
	void Camera::setOrthographic(float halfHeight, float aspect, float nearZ, float farZ)
	{
		mode_ = Projection::Orthographic;
		orthoHeight_ = halfHeight;
		nearZ_ = nearZ;
		farZ_ = farZ;

		if (aspect > 0.f)
			aspect_ = aspect;

		projDirty_ = true;
		viewProjDirty_ = true;
		frustumDirty_ = true;
	}

	/*!
		@brief
			Selects the projection mode, keeping the existing parameters.
		@param mode
			Perspective or orthographic
		*/
	void Camera::setProjection(Projection mode)
	{
		mode_ = mode;
		projDirty_ = true;
		viewProjDirty_ = true;
		frustumDirty_ = true;
	}

	/*!
		@brief
			Sets the aspect ratio, ignoring values that are not strictly positive.
		@param aspect
			Width / height
		*/
	void Camera::setAspect(float aspect)
	{
		// Rejects 0 (minimized window) and NaN.
		if (!(aspect > 0.f))
			return;

		aspect_ = aspect;
		projDirty_ = true;
		viewProjDirty_ = true;
		frustumDirty_ = true;
	}

	/*!
		@brief
			Sets the vertical field of view used by the perspective projection.
		@param fovYRad
			Vertical field of view, in radians
		*/
	void Camera::setFovY(float fovYRad)
	{
		fovY_ = fovYRad;
		projDirty_ = true;
		viewProjDirty_ = true;
		frustumDirty_ = true;
	}

	/*!
		@brief
			Sets the near and far clip distances.
		@param nearZ
			Near clip distance
		@param farZ
			Far clip distance
		*/
	void Camera::setClip(float nearZ, float farZ)
	{
		nearZ_ = nearZ;
		farZ_ = farZ;
		projDirty_ = true;
		viewProjDirty_ = true;
		frustumDirty_ = true;
	}

	/*!
		@brief
			Sets the half height of the orthographic view volume.
		@param halfHeight
			Half the vertical extent, in world units
		*/
	void Camera::setOrthoHeight(float halfHeight)
	{
		orthoHeight_ = halfHeight;
		projDirty_ = true;
		viewProjDirty_ = true;
		frustumDirty_ = true;
	}

	// ------------------------- Accessors -------------------------

	/*!
		@brief
			Camera right axis, read back out of the cached view matrix.
		@return
			Unit-length world-space right vector
		*/
	glm::vec3 Camera::right() const
	{
		const glm::mat4& v = view();
		return { v[0][0], v[1][0], v[2][0] };
	}

	/*!
		@brief
			Camera up axis, read back out of the cached view matrix.
		@return
			Unit-length world-space up vector
		*/
	glm::vec3 Camera::up() const
	{
		const glm::mat4& v = view();
		return { v[0][1], v[1][1], v[2][1] };
	}

	/*!
		@brief
			Camera forward axis, read back out of the cached view matrix.
		@return
			Unit-length world-space forward vector
		*/
	glm::vec3 Camera::forward() const
	{
		const glm::mat4& v = view();
		return { -v[0][2], -v[1][2], -v[2][2] };
	}

	/*!
		@brief
			View matrix, recomputed first if dirty.
		@return
			The cached view matrix
		*/
	const glm::mat4& Camera::view() const
	{
		if (viewDirty_)
			recomputeView();

		return view_;
	}

	/*!
		@brief
			Projection matrix, recomputed first if dirty.
		@return
			The cached projection matrix
		*/
	const glm::mat4& Camera::projection() const
	{
		if (projDirty_)
			recomputeProj();

		return proj_;
	}

	/*!
		@brief
			Combined projection * view matrix, recomputed first if it or either input is dirty.
		@return
			The cached view-projection matrix
		*/
	const glm::mat4& Camera::viewProjection() const
	{
		if (viewProjDirty_ || viewDirty_ || projDirty_)
			recomputeViewProj();

		return viewProj_;
	}

	// -------------------------- Culling --------------------------

	/*!
		@brief
			The six view-volume planes, re-extracted if anything they depend on is dirty.
		@return
			Inward-facing, normalised clipping planes
		*/
	const Frustum& Camera::frustum() const
	{
		if (frustumDirty_ || viewProjDirty_ || viewDirty_ || projDirty_)
			recomputeFrustum();

		return frustum_;
	}

	/*!
		@brief
			Sphere / frustum overlap test.
		@param center
			Sphere centre in world space
		@param radius
			Sphere radius
		@return
			false only if the sphere is wholly outside the view volume
		*/
	bool Camera::intersects(const glm::vec3& center, float radius) const
	{
		const Frustum& f = frustum();

		for (const Plane& p : f.planes_)
		{
			if (p.signedDistance(center) < -radius)
				return false;
		}

		return true;
	}

	/*!
		@brief
			Axis-aligned box / frustum overlap test using the p-vertex of each plane.
		@param mn
			Minimum corner
		@param mx
			Maximum corner
		@return
			false only if the box is wholly outside the view volume
		*/
	bool Camera::intersects(const glm::vec3& mn, const glm::vec3& mx) const
	{
		const Frustum& f = frustum();

		for (const Plane& p : f.planes_)
		{
			// p-vertex: the corner furthest along the plane normal. If even
			// that corner is behind the plane, the whole box is.
			const glm::vec3 pv
			{
				p.n_.x >= 0.f ? mx.x : mn.x,
				p.n_.y >= 0.f ? mx.y : mn.y,
				p.n_.z >= 0.f ? mx.z : mn.z
			};

			if (p.signedDistance(pv) < 0.f)
				return false;
		}

		return true;
	}

	// ------------------------ Recomputation ----------------------

	/*!
		@brief
			Rebuilds the view matrix from position, yaw and pitch and clears its dirty flag.
		*/
	void Camera::recomputeView() const
	{
		const float cy = std::cos(yaw_);
		const float sy = std::sin(yaw_);
		const float cp = std::cos(pitch_);
		const float sp = std::sin(pitch_);

		const glm::vec3 f{ -sy * cp, sp, -cy * cp };

		const glm::vec3 r{ cy, 0.f, -sy };
		const glm::vec3 u = glm::cross(r, f);

		view_ = glm::mat4(1.f);

		view_[0][0] = r.x;	view_[1][0] = r.y;	view_[2][0] = r.z;
		view_[0][1] = u.x;	view_[1][1] = u.y;	view_[2][1] = u.z;
		view_[0][2] = -f.x;	view_[1][2] = -f.y;	view_[2][2] = -f.z;

		view_[3][0] = -glm::dot(r, position_);
		view_[3][1] = -glm::dot(u, position_);
		view_[3][2] = glm::dot(f, position_);

		viewDirty_ = false;
	}

	/*!
		@brief
			Rebuilds the perspective or orthographic projection matrix and clears its dirty flag.
		*/
	void Camera::recomputeProj() const
	{
		if (mode_ == Projection::Perspective)
		{
			proj_ = glm::perspective(fovY_, aspect_, nearZ_, farZ_);
		}
		else
		{
			const float halfH = orthoHeight_;
			const float halfW = halfH * aspect_;

			proj_ = glm::ortho(-halfW, halfW, -halfH, halfH, nearZ_, farZ_);
		}

		projDirty_ = false;
	}

	/*!
		@brief
			Rebuilds the view-projection matrix and clears its dirty flag.
		*/
	void Camera::recomputeViewProj() const
	{
		viewProj_ = projection() * view();
		viewProjDirty_ = false;
	}

	/*!
		@brief
			Extracts the six frustum planes from the view-projection (Gribb-Hartmann) and clears its dirty flag.
		*/
	void Camera::recomputeFrustum() const
	{
		const glm::mat4& m = viewProjection();

		const glm::vec4 r0 = row(m, 0);
		const glm::vec4 r1 = row(m, 1);
		const glm::vec4 r2 = row(m, 2);
		const glm::vec4 r3 = row(m, 3);

		// Gribb-Hartmann
		frustum_.planes_[Frustum::Left]		= makePlane(r3 + r0);
		frustum_.planes_[Frustum::Right]	= makePlane(r3 - r0);
		frustum_.planes_[Frustum::Bottom]	= makePlane(r3 + r1);
		frustum_.planes_[Frustum::Top]		= makePlane(r3 - r1);
		frustum_.planes_[Frustum::Near]		= makePlane(r3 + r2);
		frustum_.planes_[Frustum::Far]		= makePlane(r3 - r2);

		frustumDirty_ = false;
	}
}
