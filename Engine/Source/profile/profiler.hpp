/*************************************************************************************************************
\file       profiler.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      CPU instrumentation: the VOX_PROFILE_* macros over Tracy (empty without VOX_PROFILER_ENABLED),
            zone colours, build info and thread naming. The only profiling header engine and editor
            code includes; see the usage guide and silent-failure rules in the comment below.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// ---------------------------------------------------------------------------
//  CPU instrumentation. The only profiling header engine and editor code
//  includes; nothing outside Engine/Source/profile names Tracy directly.
//
//  Every macro expands to nothing unless VOX_PROFILER_ENABLED is defined, which
//  happens by linking vendor_tracy with the VOX_PROFILER CMake option on. An
//  instrumented function therefore costs nothing in a build without it, and
//  with it costs a few nanoseconds per zone until a viewer connects
//  (TRACY_ON_DEMAND).
//
//  To capture: unzip the Tracy viewer of the client's exact version
//  (windows-<version>.zip from github.com/wolfpld/tracy/releases; the version
//  is pinned in Vendor/CMakeLists.txt) into Tools/Tracy/, run the app, then
//  Window > Profiler > "Open Tracy and connect" in the Editor, or start
//  tracy-profiler.exe yourself. Nothing is recorded before it connects, so to
//  profile a level load, connect first. Profile Release, not Debug.
//
//      void System::step(float dt)
//      {
//          VOX_PROFILE_FUNCTION();                     // zone named after the function
//          {
//              VOX_PROFILE_SCOPE("Characters");        // named zone for this block
//              VOX_PROFILE_VALUE(characters_.size());  // number shown on the zone
//              ...
//          }
//      }
//
//  Rules that fail silently when broken:
//
//  - Names passed to SCOPE, PLOT, FRAME_NAMED, LOCKABLE and MESSAGE_LITERAL
//    must be string LITERALS. Tracy keeps the pointer and reads it later from
//    another thread. For text built at run time use VOX_PROFILE_TEXT (copied)
//    or VOX_PROFILE_SCOPE_DYNAMIC (copied, and slower - an allocation per zone).
//  - A plot is identified by its name POINTER, so plot one series from one
//    place, through one literal.
//  - Zones are scoped to a thread. Never begin one on a thread and end it on
//    another; the RAII macros make that impossible as long as the zone is not
//    moved into a lambda.
//  - One zone per C++ scope. Each zone macro declares the same variable (so
//    VOX_PROFILE_TEXT can find it), and a second one in the same block is a
//    redefinition error. Open a { } block for it - which is also what makes
//    the zone end where you mean it to.
//  - Macro arguments are NOT evaluated in a build without the profiler. Never
//    put work inside one: VOX_PROFILE_VALUE(queue.drain()) drains nothing
//    there. Compute first, then pass the variable.
//  - GPU zones live in profiler_gpu.hpp, because they need GL headers.
//
//  The zone colours below are 0xRRGGBB and only group things by eye in the
//  viewer; zero means "pick one for me".
// ---------------------------------------------------------------------------

// stds
#include <cstddef>
#include <cstdint>

#if defined(VOX_PROFILER_ENABLED)
	// third-party
	#include <tracy/Tracy.hpp>
#endif

// Headers that use VOX_PROFILE_LOCKABLE change layout with the profiler
// (util/thread/job_queue.hpp does). A target that reaches into Engine/Source
// without linking vendor_tracy would compile those classes differently from
// Engine.lib and corrupt memory at run time. This turns that into a link
// error (LNK2038) naming the mismatch. Fix: link vendor_tracy, as Editor and
// Tools/VoxelProbe do.
#if defined(_MSC_VER)
	#if defined(VOX_PROFILER_ENABLED)
		#pragma detect_mismatch("VOX_PROFILER", "enabled")
	#else
		#pragma detect_mismatch("VOX_PROFILER", "disabled")
	#endif
#endif


namespace profile
{
	/** Zone colours by subsystem, so a lane reads at a glance in the viewer. */
	namespace colour
	{
		inline constexpr std::uint32_t kInput		= 0x4FA3D1;
		inline constexpr std::uint32_t kPhysics		= 0xD9822B;
		inline constexpr std::uint32_t kGraphics	= 0x3C9D5B;
		inline constexpr std::uint32_t kVoxel		= 0x8E6FD1;
		inline constexpr std::uint32_t kAudio		= 0xC94F7C;
		inline constexpr std::uint32_t kScript		= 0xD1B84F;
		inline constexpr std::uint32_t kResource	= 0x6F8FA8;
		inline constexpr std::uint32_t kEditor		= 0x9A9A9A;
		inline constexpr std::uint32_t kWait		= 0x555555;	// blocked, not working
	}

	/** How the build was configured; the Editor's Profiler panel shows it. */
	struct BuildInfo
	{
		bool		enabled_		{ false };	// VOX_PROFILER
		bool		memory_			{ false };	// VOX_PROFILER_MEMORY
		bool		remote_			{ false };	// VOX_PROFILER_REMOTE
		const char*	clientVersion_	{ "" };		// the viewer must match it
		std::uint16_t port_			{ 0 };
	};

	/*!
		@brief
			Names the calling (main) thread and tags the capture with the app's
			name and build.

			Call once, first thing in init.
		@param application
			"Editor" or "Game"; shown in the viewer's connection list and title
		*/
	void startup(const char* application);

	/*!
		@brief
			How this binary was built. Valid without startup().
		@return
			The build configuration of the profiler
		*/
	BuildInfo buildInfo();

	/*!
		@brief
			Whether a Tracy viewer is attached.
		@return
			True while a Tracy viewer is attached and zones are being recorded.
			Always false in a build without the profiler.
		*/
	bool viewerConnected();

	/*!
		@brief
			Names the calling thread in the viewer and for the OS (so the Visual
			Studio Threads window shows it too).

			Does nothing without the profiler.
		@param name
			Copied. Convention: "<Area> worker <index>"
		@param index
			Appended to the name when not negative
		*/
	void nameThread(const char* name, int index = -1);
}


#if defined(VOX_PROFILER_ENABLED)

// ------------------------------- Frames --------------------------------

/** End of one main-loop frame. Exactly one call site: FrameLoop::tick. */
#define VOX_PROFILE_FRAME()							FrameMark

/** A secondary frame set, e.g. one mark per fixed simulation step. */
#define VOX_PROFILE_FRAME_NAMED(literal)			FrameMarkNamed(literal)

// -------------------------------- Zones --------------------------------

#define VOX_PROFILE_FUNCTION()						ZoneScoped
#define VOX_PROFILE_FUNCTION_C(rgb)					ZoneScopedC(rgb)
#define VOX_PROFILE_SCOPE(literal)					ZoneScopedN(literal)
#define VOX_PROFILE_SCOPE_C(literal, rgb)			ZoneScopedNC(literal, rgb)

/** A zone whose name is only known at run time. Allocates; keep it off hot paths. */
#define VOX_PROFILE_SCOPE_DYNAMIC(cstr)				ZoneTransientN(___tracy_scoped_zone, cstr, true)

/** Attach text or a number to the innermost zone opened in this scope. */
#define VOX_PROFILE_TEXT(ptr, size)					ZoneText(ptr, size)
#define VOX_PROFILE_TEXT_F(...)						ZoneTextF(__VA_ARGS__)
#define VOX_PROFILE_VALUE(number)					ZoneValue(static_cast<std::uint64_t>(number))

// -------------------------------- Plots --------------------------------

#define VOX_PROFILE_PLOT(literal, number)			TracyPlot(literal, number)
#define VOX_PROFILE_PLOT_CONFIG(literal, format)	TracyPlotConfig(literal, format, true, true, 0)
#define VOX_PROFILE_PLOT_NUMBER						tracy::PlotFormatType::Number
#define VOX_PROFILE_PLOT_MEMORY						tracy::PlotFormatType::Memory
#define VOX_PROFILE_PLOT_PERCENT					tracy::PlotFormatType::Percentage

// ------------------------------ Messages -------------------------------

/** A line in the viewer's message log, pinned to the timeline. */
#define VOX_PROFILE_MESSAGE(ptr, size)				TracyMessage(ptr, size)
#define VOX_PROFILE_MESSAGE_LITERAL(literal)		TracyMessageL(literal)
#define VOX_PROFILE_MESSAGE_C(ptr, size, rgb)		TracyMessageC(ptr, size, rgb)

// -------------------------------- Locks --------------------------------
//
//  Declare a mutex with VOX_PROFILE_LOCKABLE and the viewer shows who held it
//  and who waited. The declared type changes, so name it with
//  VOX_PROFILE_LOCKABLE_TYPE wherever it is spelled out:
//
//      VOX_PROFILE_LOCKABLE(std::mutex, mutex_, "JobQueue");
//      std::unique_lock<VOX_PROFILE_LOCKABLE_TYPE(std::mutex)> lock(mutex_);
//
//  and wait on it with std::condition_variable_any, because
//  std::condition_variable only accepts std::mutex.

#define VOX_PROFILE_LOCKABLE(type, name, literal)	TracyLockableN(type, name, literal)
#define VOX_PROFILE_LOCKABLE_TYPE(type)				LockableBase(type)

// ------------------------------- Memory --------------------------------
//
//  For allocators that do not go through global new/delete (pools, arenas,
//  GPU heaps). The pool name must be a literal and the same pointer for the
//  alloc and the free.

#define VOX_PROFILE_ALLOC(ptr, bytes, poolLiteral)	TracyAllocN(ptr, bytes, poolLiteral)
#define VOX_PROFILE_FREE(ptr, poolLiteral)			TracyFreeN(ptr, poolLiteral)

#else

#define VOX_PROFILE_FRAME()
#define VOX_PROFILE_FRAME_NAMED(literal)

#define VOX_PROFILE_FUNCTION()
#define VOX_PROFILE_FUNCTION_C(rgb)
#define VOX_PROFILE_SCOPE(literal)
#define VOX_PROFILE_SCOPE_C(literal, rgb)
#define VOX_PROFILE_SCOPE_DYNAMIC(cstr)

#define VOX_PROFILE_TEXT(ptr, size)
#define VOX_PROFILE_TEXT_F(...)
#define VOX_PROFILE_VALUE(number)

#define VOX_PROFILE_PLOT(literal, number)
#define VOX_PROFILE_PLOT_CONFIG(literal, format)
#define VOX_PROFILE_PLOT_NUMBER						0
#define VOX_PROFILE_PLOT_MEMORY						0
#define VOX_PROFILE_PLOT_PERCENT					0

#define VOX_PROFILE_MESSAGE(ptr, size)
#define VOX_PROFILE_MESSAGE_LITERAL(literal)
#define VOX_PROFILE_MESSAGE_C(ptr, size, rgb)

#define VOX_PROFILE_LOCKABLE(type, name, literal)	type name
#define VOX_PROFILE_LOCKABLE_TYPE(type)				type

#define VOX_PROFILE_ALLOC(ptr, bytes, poolLiteral)
#define VOX_PROFILE_FREE(ptr, poolLiteral)

#endif
