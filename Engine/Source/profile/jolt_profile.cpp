/*************************************************************************************************************
\file       jolt_profile.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Jolt -> Tracy. With JPH_EXTERNAL_PROFILE (set on vendor_jolt whenever VOX_PROFILER is on)
            every JPH_PROFILE scope inside Jolt constructs an ExternalProfileMeasurement, and a
            statically linked Jolt leaves that class for the application to define. This is that
            definition: each measurement is a Tracy zone, so the broad phase, narrow phase, solver
            islands and every job on the "Jolt worker" threads show up on the timeline with no Jolt
            changes. It lives in profile/ rather than physics/ because it is the one place besides
            profiler.hpp that has to name Tracy types.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

// Jolt
#include <Jolt/Jolt.h>
#include <Jolt/Core/Profiler.h>

#if defined(JPH_EXTERNAL_PROFILE)

#include "profiler.hpp"

// stds
#include <mutex>
#include <new>
#include <unordered_map>


namespace
{
	using tracy::SourceLocationData;

	/*!
		@brief
			Returns the immortal Tracy source location for a Jolt scope name,
			creating it on first use.

			Jolt hands over a name per scope, always a literal or a function name,
			so the pointer identifies the scope. Tracy wants one immortal
			SourceLocationData per scope: it keeps the pointer and reads it from its
			own thread later. They are created on first use and never freed; there
			are a few hundred distinct scopes in Jolt.

			The process-wide table keeps one location per scope, so the viewer's
			statistics do not split a scope by thread. The thread-local table in
			front of it keeps the lock off the hot path.
		@param name
			Jolt's scope name; its pointer is the key
		@param colour
			Jolt colour (R in the low byte), 0 for the default physics colour
		@return
			The source location for the scope
		*/
	const SourceLocationData* locationFor(const char* name, JPH::uint32 colour)
	{
		thread_local std::unordered_map<const char*, const SourceLocationData*> local;

		if (const auto it = local.find(name); it != local.end())
			return it->second;

		static std::mutex mutex;
		static std::unordered_map<const char*, const SourceLocationData*> shared;

		const SourceLocationData* location = nullptr;

		{
			const std::lock_guard<std::mutex> lock(mutex);

			auto [it, inserted] = shared.try_emplace(name, nullptr);

			if (inserted)
			{
				// JPH::Color packs R in the low byte; Tracy reads 0xRRGGBB.
				const std::uint32_t rgb = colour == 0
					? profile::colour::kPhysics
					: ((colour & 0xffu) << 16) | (colour & 0xff00u) | ((colour >> 16) & 0xffu);

				it->second = new SourceLocationData{ name, "Jolt", "Jolt", 0, rgb };
			}

			location = it->second;
		}

		local.emplace(name, location);
		return location;
	}
}

JPH_NAMESPACE_BEGIN

// mUserData is 64 bytes aligned to 16; a ScopedZone is a bool and, under
// TRACY_ON_DEMAND, a connection id.
static_assert(sizeof(tracy::ScopedZone) <= 64, "tracy::ScopedZone no longer fits Jolt's ExternalProfileMeasurement::mUserData");
static_assert(alignof(tracy::ScopedZone) <= 16, "tracy::ScopedZone is over-aligned for Jolt's ExternalProfileMeasurement::mUserData");

/*!
	@brief
		Opens a Tracy zone for a Jolt profile scope, constructed in mUserData.
	@param inName
		Jolt's scope name
	@param inColor
		Jolt's scope colour
	*/
ExternalProfileMeasurement::ExternalProfileMeasurement(const char* inName, uint32 inColor)
{
	new (mUserData) tracy::ScopedZone(locationFor(inName, inColor), 0, true);
}

/*!
	@brief
		Closes the Tracy zone held in mUserData.
	*/
ExternalProfileMeasurement::~ExternalProfileMeasurement()
{
	std::launder(reinterpret_cast<tracy::ScopedZone*>(mUserData))->~ScopedZone();
}

JPH_NAMESPACE_END

#endif
