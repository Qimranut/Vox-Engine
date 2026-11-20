/*************************************************************************************************************
\file       contact_listener.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      ContactQueue implementation: Jolt contact callbacks copy what they need into records under a
            mutex, and drain() hands the queue to the main thread.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "contact_listener.hpp"

// Math
#include "../math/jolt_glm.hpp"

// Jolt
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>


namespace physics
{
	/*!
		@brief
			Queues an Added record: body ids, user data, sensor flags, first contact point, normal and pre-solve approach speed.
		@param body1
			First body of the pair
		@param body2
			Second body of the pair
		@param manifold
			Contact points and normal from Jolt
		@param settings
			Jolt contact settings (unused)
		*/
	void ContactQueue::OnContactAdded(const JPH::Body& body1,
									  const JPH::Body& body2,
									  const JPH::ContactManifold& manifold,
									  JPH::ContactSettings& /*settings*/)
	{
		ContactRecord r;
		r.kind_			= ContactRecord::Kind::Added;
		r.body1_		= body1.GetID().GetIndexAndSequenceNumber();
		r.body2_		= body2.GetID().GetIndexAndSequenceNumber();
		r.user1_		= body1.GetUserData();
		r.user2_		= body2.GetUserData();
		r.sensor_		= body1.IsSensor() || body2.IsSensor();
		r.body1IsSensor_ = body1.IsSensor();

		const JPH::RVec3 p = manifold.GetWorldSpaceContactPointOn1(0);
		const JPH::Vec3 n = manifold.mWorldSpaceNormal;

		r.point_		= toGlm(JPH::Vec3(p));
		r.normal_		= toGlm(n);

		// The velocities are still pre-solve here, which is exactly what an
		// impact strength wants. The normal points from body 1 to body 2, so
		// body 1 closing on body 2 is positive.
		const JPH::Vec3 v1 = body1.GetPointVelocity(p);
		const JPH::Vec3 v2 = body2.GetPointVelocity(p);

		r.approachSpeed_ = (v1 - v2).Dot(n);

		std::lock_guard lock{ mtx_ };
		queue_.push_back(r);
	}

	/*!
		@brief
			Queues a Removed record from the pair's body ids, without touching the bodies.
		@param pair
			Sub-shape pair whose contact ended
		*/
	void ContactQueue::OnContactRemoved(const JPH::SubShapeIDPair& pair)
	{
		// The bodies must not be touched here - they are locked, and may
		// already be destroyed. Ids only.
		ContactRecord r;
		r.kind_		= ContactRecord::Kind::Removed;
		r.body1_	= pair.GetBody1ID().GetIndexAndSequenceNumber();
		r.body2_	= pair.GetBody2ID().GetIndexAndSequenceNumber();

		std::lock_guard lock{ mtx_ };
		queue_.push_back(r);
	}

	/*!
		@brief
			Moves every queued record out under the lock.
		@param out
			Cleared, then filled in the order Jolt reported
		*/
	void ContactQueue::drain(std::vector<ContactRecord>& out)
	{
		out.clear();

		std::lock_guard lock{ mtx_ };
		out.swap(queue_);
	}
}
