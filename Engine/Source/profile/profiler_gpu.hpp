/*************************************************************************************************************
\file       profiler_gpu.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      GPU instrumentation: the VOX_PROFILE_GPU_* macros over Tracy's OpenGL zones, split from
            profiler.hpp because they need GL headers. See the rules in the comment below.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// ---------------------------------------------------------------------------
//  GPU instrumentation: the OpenGL half of profiler.hpp, split out because it
//  drags in GL headers. Include it only from files that already talk to GL.
//
//  A GPU zone brackets the GL commands issued inside the scope with two
//  timestamp queries; the viewer draws them on a separate "OpenGL" lane, in
//  GPU time, under the CPU zone that submitted them.
//
//  - Only on the thread that owns the GL context, and only after
//    VOX_PROFILE_GPU_CONTEXT() ran there (graphics::System::init). A GPU zone
//    hit before that, with a viewer attached, dereferences a null context.
//  - VOX_PROFILE_GPU_COLLECT() once per frame, after the swap: it reads back
//    the finished queries. Skip it and the 64k query ring fills up.
//  - Names are string literals, as for CPU zones.
//  - Pair each GPU zone with a CPU zone of the same name. They measure
//    different things: the CPU zone is the cost of SUBMITTING the work.
// ---------------------------------------------------------------------------

#include "profiler.hpp"

// third-party
#include <GL/glew.h>

#if defined(VOX_PROFILER_ENABLED)

#include <tracy/TracyOpenGL.hpp>

#define VOX_PROFILE_GPU_CONTEXT()				TracyGpuContext
#define VOX_PROFILE_GPU_CONTEXT_NAME(literal)	TracyGpuContextName(literal, static_cast<std::uint16_t>(sizeof(literal) - 1))
#define VOX_PROFILE_GPU_SCOPE(literal)			TracyGpuZone(literal)
#define VOX_PROFILE_GPU_SCOPE_C(literal, rgb)	TracyGpuZoneC(literal, rgb)
#define VOX_PROFILE_GPU_COLLECT()				TracyGpuCollect

#else

#define VOX_PROFILE_GPU_CONTEXT()
#define VOX_PROFILE_GPU_CONTEXT_NAME(literal)
#define VOX_PROFILE_GPU_SCOPE(literal)
#define VOX_PROFILE_GPU_SCOPE_C(literal, rgb)
#define VOX_PROFILE_GPU_COLLECT()

#endif
