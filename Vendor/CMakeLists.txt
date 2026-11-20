include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/libraries")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
find_package(OpenGL REQUIRED)

# Existing vendored headers and sources stay at their current versions.
# json is nlohmann/json 3.11.3 (single header, MIT), used by the voxel
# subsystem for material tables and terrain recipes.
foreach(name glm entt)
    add_library(vendor_${name} INTERFACE)
    target_include_directories(vendor_${name} SYSTEM INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}")
endforeach()

FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

FetchContent_MakeAvailable(json)

add_library(vendor_json INTERFACE)
target_link_libraries(vendor_json INTERFACE nlohmann_json)

# stb is tools-only under the approved-library list: only Tools/VoxelProbe links
# this, for its preview PNG. Engine, Editor and Game never do. Image decoding is
# TextureCompiler's job (DirectXTex), so stb_image is not compiled at all.
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/stb_image_write.cpp" CONTENT
    "#define STB_IMAGE_WRITE_IMPLEMENTATION\n#include <stb_image_write.h>\n" @ONLY)
add_library(vendor_stb STATIC "${CMAKE_CURRENT_BINARY_DIR}/stb_image_write.cpp")
target_include_directories(vendor_stb SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/stb")

file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lua/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
add_library(vendor_lua STATIC ${LUA_SOURCES})
target_include_directories(vendor_lua SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lua")

add_library(vendor_sol2 INTERFACE)
target_include_directories(vendor_sol2 SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/sol2/include")
target_link_libraries(vendor_sol2 INTERFACE vendor_lua)
target_compile_definitions(vendor_sol2 INTERFACE $<$<CONFIG:Debug>:SOL_ALL_SAFETIES_ON=1>)

add_library(vendor_glew STATIC "${CMAKE_CURRENT_SOURCE_DIR}/glew/src/glew.c")
target_include_directories(vendor_glew SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/glew/include")
target_compile_definitions(vendor_glew PUBLIC GLEW_STATIC)
target_link_libraries(vendor_glew PUBLIC OpenGL::GL)

# Build GLFW 3.4 from source so Debug uses /MDd and Release uses /MD.
# The old prebuilt glfw3.lib used the Release runtime in every configuration.
FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
    URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
foreach(feature DOCS TESTS EXAMPLES)
    set(GLFW_BUILD_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)
add_library(vendor_glfw ALIAS glfw)

# ---------------------------------------------------------------------------
#  Jolt Physics 5.6.0 - built from the 153 .cpp files in this drop, NOT from
#  the prebuilt Jolt.lib files deleted during the prune.
#
#  This is not only about repo size. Jolt's JPH_* defines change struct layout
#  in its headers. Linking a prebuilt .lib built with one set of defines
#  against code compiled with another is an ODR violation that shows up as
#  memory corruption, not a link error. Declaring them PUBLIC on a from-source
#  target makes a mismatch impossible.
#
#  JPH_ENABLE_ASSERTS (Debug) turns on Jolt's API-misuse checks - locking a
#  body during an update, bad layers, invalid shapes. physics::System routes
#  failures to stderr and breaks into the debugger.
#  JPH_DEBUG_RENDERER is deliberately NOT defined: the body wireframe view was
#  removed for performance (it walked every triangle of every terrain mesh body
#  each frame), and without the define Jolt compiles no debug geometry support.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE JOLT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Jolt/include/Jolt/*.cpp")
add_library(vendor_jolt STATIC ${JOLT_SOURCES})
target_include_directories(vendor_jolt SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Jolt/include")
target_compile_definitions(vendor_jolt PUBLIC
    JPH_OBJECT_STREAM
    $<$<CONFIG:Debug>:JPH_ENABLE_ASSERTS>
    $<$<CONFIG:Debug>:JPH_FLOATING_POINT_EXCEPTIONS_ENABLED>)
# Jolt's own JPH_PROFILE scopes (broad phase, narrow phase, solver, each job)
# are routed into Tracy: with JPH_EXTERNAL_PROFILE Jolt leaves
# ExternalProfileMeasurement for us to define, which
# Engine/Source/profile/jolt_profile.cpp does. That is what puts the
# physics job threads on the timeline. Without the profiler, Debug keeps Jolt's
# built-in sampler as before. The two are alternatives, never both.
if(VOX_PROFILER)
    target_compile_definitions(vendor_jolt PUBLIC JPH_EXTERNAL_PROFILE)
else()
    target_compile_definitions(vendor_jolt PUBLIC $<$<CONFIG:Debug>:JPH_PROFILE_ENABLED>)
endif()

# FreeType is not built. It is tools-only under the approved-library list and
# nothing used it; a future font-atlas baker under Tools/ can fetch it again.

# Assimp is tools-only under the approved-library list: only Tools/ModelCompiler
# links it. The runtime reads the .vmdl files that tool writes.
foreach(feature TESTS ASSIMP_TOOLS SAMPLES)
    set(ASSIMP_BUILD_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
foreach(format FBX GLTF OBJ COLLADA)
    set(ASSIMP_BUILD_${format}_IMPORTER ON CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(assimp EXCLUDE_FROM_ALL)

# FMOD remains the locally supplied SDK, with logging DLLs for Debug.
set(FMOD_API "${CMAKE_CURRENT_SOURCE_DIR}/fmod/api")
foreach(component core studio)
    if(component STREQUAL "core")
        set(name fmod)
    else()
        set(name fmodstudio)
    endif()
    set(root "${FMOD_API}/${component}")
    foreach(file "${name}_vc.lib" "${name}L_vc.lib" "${name}.dll" "${name}L.dll")
        if(NOT EXISTS "${root}/lib/x64/${file}")
            message(FATAL_ERROR "Missing FMOD SDK file: ${root}/lib/x64/${file}")
        endif()
    endforeach()
    add_library(vendor_${name} SHARED IMPORTED GLOBAL)
    set_target_properties(vendor_${name} PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_IMPLIB_DEBUG "${root}/lib/x64/${name}L_vc.lib"
        IMPORTED_LOCATION_DEBUG "${root}/lib/x64/${name}L.dll"
        IMPORTED_IMPLIB_RELEASE "${root}/lib/x64/${name}_vc.lib"
        IMPORTED_LOCATION_RELEASE "${root}/lib/x64/${name}.dll"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        INTERFACE_INCLUDE_DIRECTORIES "${root}/inc")
endforeach()
target_link_libraries(vendor_fmodstudio INTERFACE vendor_fmod)

# Same ImGui docking version and GLFW/OpenGL backends as the reference.
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.5-docking.tar.gz
    URL_HASH SHA256=c816c20e8c75f3e15ae867350e79925502d1a6a85938bb1a73b8927e5f31f9cb
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR custom-target)
FetchContent_MakeAvailable(imgui)
add_library(vendor_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
target_include_directories(vendor_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_link_libraries(vendor_imgui PUBLIC vendor_glfw OpenGL::GL)

# Editor-only 3D transform widget. Pin the existing revision and verify the
# archive; only compile the gizmo, not upstream examples or other widgets.
FetchContent_Declare(imguizmo
    URL https://github.com/CedricGuillemet/ImGuizmo/archive/18cef5e031d8c6973d80284c67f60549fafd78c1.tar.gz
    URL_HASH SHA256=6ad626f0687be12c2f3ba6542c0f1bdda9e71e395d0645e4cda37695354406d8
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR custom-target)
FetchContent_MakeAvailable(imguizmo)
add_library(vendor_imguizmo STATIC "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
target_include_directories(vendor_imguizmo SYSTEM PUBLIC "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(vendor_imguizmo PUBLIC vendor_imgui)
target_compile_options(vendor_imguizmo PRIVATE /W0)
set_target_properties(vendor_imguizmo PROPERTIES FOLDER "Vendor")

# ---------------------------------------------------------------------------
#  Tracy 0.14.1 - the profiler client behind Engine/Source/profile. Our code
#  never includes a Tracy header directly: it uses the VOX_PROFILE_* macros in
#  profile/profiler.hpp, which expand to nothing without VOX_PROFILER_ENABLED.
#
#  vendor_tracy always exists, so Engine and Editor link it unconditionally.
#  With VOX_PROFILER=OFF it is an empty INTERFACE target: nothing is fetched,
#  compiled or linked.
#
#  - The viewer must be the SAME version (the wire protocol is versioned):
#    windows-0.14.1.zip from the Tracy release page, unzipped into Tools/Tracy/.
#  - TRACY_ON_DEMAND: nothing is recorded until a viewer connects. Without it
#    the client queues every event from process start until someone connects,
#    which in an editor left open for hours is unbounded memory.
#  - Unless VOX_PROFILER_REMOTE is on, the client listens on localhost only
#    (TRACY_ONLY_LOCALHOST), so no firewall prompt appears and nobody else on
#    the network can attach to a teammate's editor. Its discovery broadcast
#    then goes to 127.255.255.255, so a viewer on the same machine still lists
#    the running app by itself.
#  - Tracy's defines change the layout of its inline types (ScopedZone grows a
#    connection id under ON_DEMAND), so they must reach every translation unit
#    that expands a macro. Its CMake declares them PUBLIC on TracyClient; keep
#    going through vendor_tracy rather than adding the include path by hand.
# ---------------------------------------------------------------------------
add_library(vendor_tracy INTERFACE)
if(VOX_PROFILER)
    FetchContent_Declare(tracy
        URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
        URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    function(vox_configure_tracy)
        # Its options are cache entries, so FORCE keeps a flipped
        # VOX_PROFILER_REMOTE from being ignored on the next configure.
        set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
        set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
        if(VOX_PROFILER_REMOTE)
            set(TRACY_ONLY_LOCALHOST OFF CACHE BOOL "" FORCE)
        else()
            set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
        endif()
        # An AMD ROCm probe that only warns on Windows.
        set(CMAKE_DISABLE_FIND_PACKAGE_rocprofiler-sdk TRUE)
        FetchContent_MakeAvailable(tracy)
    endfunction()
    vox_configure_tracy()
    target_link_libraries(vendor_tracy INTERFACE TracyClient)
    target_compile_definitions(vendor_tracy INTERFACE
        VOX_PROFILER_ENABLED=1
        $<$<BOOL:${VOX_PROFILER_MEMORY}>:VOX_PROFILER_MEMORY=1>
        $<$<BOOL:${VOX_PROFILER_REMOTE}>:VOX_PROFILER_REMOTE=1>)
endif()

# ---------------------------------------------------------------------------
#  DirectXTex (may2026) - BC1/BC3/BC4/BC5/BC6H/BC7 encoders, sRGB-correct
#  mipmaps and the DDS writer behind Tools/TextureCompiler. It is an OFFLINE
#  tool dependency: Engine, Editor and Game never link it, and the runtime
#  keeps reading DDS through vox::parseDds.
#
#  - Only the library is built. Its texconv/DDSView targets and the DX11/DX12
#    GPU paths are off (the GPU encoder needs fxc.exe at configure time).
#  - OpenMP is off. MSVC's OpenMP runtime (vcomp140d.dll in Debug) is not on
#    PATH, so the tool would fail to start. TextureCompiler parallelises the
#    BC6H/BC7 encode itself, by compressing strips of blocks on std::threads.
#  - The BC6H/BC7 encoders are a brute-force search and take minutes per
#    texture unoptimised. Debug builds of this target (and the tool) use /O2
#    without /RTC1; the CRT stays /MDd, so iterator-debug levels still match.
#  - Its include(CTest) would add dashboard targets (Continuous, Nightly, ...)
#    to the solution; CTEST_TARGETS_ADDED suppresses those, not tests.
# ---------------------------------------------------------------------------
FetchContent_Declare(directxtex
    URL https://github.com/microsoft/DirectXTex/archive/refs/tags/may2026.tar.gz
    URL_HASH SHA256=204ae68a66fb15d87867d836efbac9ae5aa799ca2a1867024d96a8168fa3651f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
function(vox_configure_directxtex)
    foreach(feature TOOLS SAMPLE DX11 DX12)
        set(BUILD_${feature} OFF)
    endforeach()
    set(BUILD_TESTING OFF)
    set(BC_USE_OPENMP OFF)
    set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)
    string(REPLACE "/Od" "/O2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Ob0" "/Ob2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    FetchContent_MakeAvailable(directxtex)
endfunction()
vox_configure_directxtex()
add_library(vendor_directxtex ALIAS DirectXTex)

# /W0 maps to one Visual Studio warning-level setting, avoiding /W1 + /w conflicts.
foreach(target vendor_lua vendor_glew vendor_jolt vendor_stb vendor_imgui DirectXTex)
    target_compile_options(${target} PRIVATE /W0)
endforeach()
if(TARGET TracyClient)
    target_compile_options(TracyClient PRIVATE /W0)
endif()
foreach(target vendor_lua vendor_glew vendor_jolt vendor_stb vendor_imgui glfw assimp zlibstatic DirectXTex TracyClient)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES FOLDER "Vendor")
    endif()
endforeach()
