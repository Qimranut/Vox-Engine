/*************************************************************************************************************
\file       profiler.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements the profiler's runtime functions: startup (thread name, program name, app info),
            build info, viewer connection state and thread naming. All no-ops without the profiler.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "profiler.hpp"

// stds
#include <cstdio>
#include <cstring>

#if defined(VOX_PROFILER_ENABLED)
	// third-party
	#include <common/TracySystem.hpp>
	#include <common/TracyVersion.hpp>
#endif


namespace profile::detail
{
	/*!
		@brief
			Defined in profiler_memory.cpp; referencing it pulls the global
			new/delete replacements out of Engine.lib.
		*/
	void linkMemoryHooks();
}

namespace profile
{
	namespace
	{
#if defined(VOX_PROFILER_ENABLED)
		/*!
			@brief
				The Tracy client version as text, e.g. "0.14.1".

				The viewer refuses a client of another protocol version, so this
				is what the Editor tells people to download.
			@return
				A static, formatted version string
			*/
		const char* clientVersion()
		{
			static char text[32]{};

			if (text[0] == '\0')
			{
				std::snprintf(text, sizeof(text), "%d.%d.%d",
					tracy::Version::Major, tracy::Version::Minor, tracy::Version::Patch);
			}

			return text;
		}
#endif
	}

	/*!
		@brief
			Links the memory hooks, names the main thread and tags the capture
			with the app's name and build.
		@param application
			"Editor" or "Game"; must be a literal, Tracy keeps the pointer
		*/
	void startup(const char* application)
	{
		detail::linkMemoryHooks();

#if defined(VOX_PROFILER_ENABLED)
		tracy::SetThreadName("Main");

		// The viewer's connection list and window title. Tracy keeps the
		// pointer, and callers pass a literal.
		TracySetProgramName(application);

#if defined(NDEBUG)
		constexpr const char* kConfig = "Release";
#else
		constexpr const char* kConfig = "Debug (timings are not representative; profile Release)";
#endif

#if defined(VOX_PROFILER_MEMORY)
		constexpr const char* kMemory = ", memory tracking on";
#else
		constexpr const char* kMemory = "";
#endif

		// Shown on the viewer's Info page, and saved with the capture.
		char info[160]{};
		std::snprintf(info, sizeof(info), "voxEngine %s - %s build%s", application, kConfig, kMemory);
		TracyAppInfo(info, std::strlen(info));
#else
		(void)application;
#endif
	}

	/*!
		@brief
			How this binary was built.
		@return
			Profiler, memory and remote flags, client version and port
		*/
	BuildInfo buildInfo()
	{
		BuildInfo info;

#if defined(VOX_PROFILER_ENABLED)
		info.enabled_ = true;
		info.clientVersion_ = clientVersion();

		// The first free port from here up: a second instrumented process on
		// the same machine (Game beside the Editor) takes the next one.
		info.port_ = 8086;
#endif

#if defined(VOX_PROFILER_MEMORY)
		info.memory_ = true;
#endif

#if defined(VOX_PROFILER_REMOTE)
		info.remote_ = true;
#endif

		return info;
	}

	/*!
		@brief
			Whether a Tracy viewer is attached.
		@return
			True while connected; always false without the profiler
		*/
	bool viewerConnected()
	{
#if defined(VOX_PROFILER_ENABLED)
		return TracyIsConnected;
#else
		return false;
#endif
	}

	/*!
		@brief
			Names the calling thread, appending the index when not negative.
		@param name
			Thread name, copied
		@param index
			Appended to the name when not negative
		*/
	void nameThread(const char* name, int index)
	{
#if defined(VOX_PROFILER_ENABLED)
		if (index < 0)
		{
			tracy::SetThreadName(name);
			return;
		}

		// SetThreadName copies.
		char text[64]{};
		std::snprintf(text, sizeof(text), "%s %d", name, index);
		tracy::SetThreadName(text);
#else
		(void)name;
		(void)index;
#endif
	}
}
