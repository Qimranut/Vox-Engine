# ============================================================================
#  Engine - the reusable static library.
#
#  If Engine compiles without knowing Editor exists, the engine is actually
#  reusable. The day a shipping build with no editor UI is needed, it already
#  works.
# ============================================================================

# CONFIGURE_DEPENDS is the critical part: it re-checks the glob at BUILD time,
# so a teammate adding a .cpp gets an automatic regeneration instead of "why
# is my new file not compiling".
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp")

add_library(Engine STATIC ${ENGINE_SOURCES})

target_include_directories(Engine
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

# Every dependency is PRIVATE. That is only legal because
# Include/vox/engine.hpp exposes no third-party type - it forward-declares
# every subsystem and holds them behind unique_ptr.
#
# The day a glm::vec3 appears in a public header, glm has to move to PUBLIC
# and Editor + Game start recompiling on every glm change. Keeping the public
# surface clean is what keeps this whole list PRIVATE.
target_link_libraries(Engine PRIVATE
    vendor_glfw
    vendor_glew
    vendor_glm
    vendor_entt
    vendor_json
    vendor_sol2          # carries vendor_lua with it
    vendor_jolt
    vendor_fmodstudio    # carries vendor_fmod with it
    vendor_json
    vendor_tracy)        # empty unless VOX_PROFILER is on; used only through profile/profiler.hpp

# No image decoder, model importer or font library here: stb, Assimp and FreeType are
# tools-only under the course's approved-library list. Textures and models reach the
# runtime already cooked by Tools/TextureCompiler and Tools/ModelCompiler.

if(MSVC)
    # /bigobj raises the per-object section limit - heavy templates (EnTT,
    # sol2) overflow it and the error message is cryptic.
    target_compile_options(Engine PRIVATE /bigobj)
else()
    target_compile_options(Engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Stable headers only. Putting frequently-edited engine headers in a PCH means
# every edit rebuilds the world.
target_precompile_headers(Engine PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/Source/pch.hpp>")

# Mirror the folder tree in Solution Explorer instead of one flat pile.
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
             PREFIX "Engine" FILES ${ENGINE_SOURCES})

set_target_properties(Engine PROPERTIES FOLDER "Engine")
