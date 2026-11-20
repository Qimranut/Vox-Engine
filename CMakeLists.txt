cmake_minimum_required(VERSION 3.24)
project(voxEngine LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use Visual Studio 2022 with the x64 architecture (Windows FMOD SDK).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_compile_options(/MP /FS)

# Profiling (Tracy). Declared ahead of Vendor, which fetches the client only
# when VOX_PROFILER is on. With it off, every VOX_PROFILE_* macro in
# Engine/Source/profile/profiler.hpp expands to nothing and no Tracy code is
# downloaded, compiled or linked - configure a submission build that way.
option(VOX_PROFILER "Compile Tracy instrumentation into Engine, Editor and Game" ON)
option(VOX_PROFILER_REMOTE "Let the Tracy viewer connect from another machine (listens on the network and announces itself)" OFF)
option(VOX_PROFILER_MEMORY "Report every global new/delete to Tracy (slower; for leak and churn hunts)" OFF)

# Dependencies keep their own warning policy; our projects use Level 4.
add_subdirectory(Vendor)
add_compile_definitions(WIN32_LEAN_AND_MEAN)
add_compile_options(/W4)

# Offline asset tools. CookAssets compresses Assets/ textures into BCn DDS and
# converts models into .vmdl, both under Assets/.cooked/, before either app
# builds, so the POST_BUILD copy below stages cooked files too. Both cookers
# are incremental. The runtime has no image decoder or model importer (stb and
# Assimp are tools-only), so an uncooked texture or model does not load. Turn
# VOX_COOK_ASSETS off only when the freshly built tools cannot run (e.g.
# blocked by Smart App Control), and cook by hand before running the apps.
#
# Textures are cooked with --quick (BC7 mode 6 only). Full BC7 is a brute-force
# search that took minutes per build for the large UI textures. For a
# full-quality cook (milestones, submission), run once by hand:
#     build\bin\<Config>\TextureCompiler.exe Assets --force
# Later quick cooks skip those files while they stay up to date.
option(VOX_COOK_ASSETS "Cook textures and models into Assets/.cooked before building the apps" ON)
add_subdirectory(Tools/TextureCompiler)
add_subdirectory(Tools/ModelCompiler)
add_subdirectory(Tools/FontCompiler)
add_custom_target(CookAssets
    COMMAND $<TARGET_FILE:TextureCompiler> "${PROJECT_SOURCE_DIR}/Assets" --quick
    COMMAND $<TARGET_FILE:ModelCompiler> "${PROJECT_SOURCE_DIR}/Assets"
    COMMAND $<TARGET_FILE:FontCompiler> "${PROJECT_SOURCE_DIR}/Assets"
    DEPENDS TextureCompiler ModelCompiler FontCompiler
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Cooking textures, models and fonts into Assets/.cooked"
    VERBATIM)
set_target_properties(CookAssets PROPERTIES FOLDER "Tools")

# Audio validation/packaging shares .cooked with generated textures.
option(VOX_COOK_AUDIO "Validate and package WAV/MP3 audio before building apps" ON)
add_subdirectory(Tools/AudioCompiler)
add_custom_target(CookAudio
    COMMAND $<TARGET_FILE:AudioCompiler> "${PROJECT_SOURCE_DIR}/Assets"
    DEPENDS AudioCompiler
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Validating and packaging audio into Assets/.cooked"
    VERBATIM)
set_target_properties(CookAudio PROPERTIES FOLDER "Tools")
# Run staging even when only an asset changed and the executable does not relink.
add_custom_target(StageAudioAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/Assets" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Assets"
    DEPENDS CookAudio
    VERBATIM)
if(VOX_COOK_ASSETS)
    add_dependencies(StageAudioAssets CookAssets)
endif()
set_target_properties(StageAudioAssets PROPERTIES FOLDER "Tools")

# Shared executable setup. Keep paths and DLL staging consistent for both apps.
function(vox_application target)
    add_executable(${target} ${ARGN}
        "${PROJECT_SOURCE_DIR}/Vendor/Jolt/include/Jolt/Jolt.natvis")
    target_link_libraries(${target} PRIVATE Engine)
    if(VOX_COOK_AUDIO)
        add_dependencies(${target} StageAudioAssets)
    endif()
    if(VOX_COOK_ASSETS)
        add_dependencies(${target} CookAssets)
    endif()
    set_target_properties(${target} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        FOLDER "Applications")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/Assets" "$<TARGET_FILE_DIR:${target}>/Assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/Scripts" "$<TARGET_FILE_DIR:${target}>/Scripts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${target}> "$<TARGET_FILE_DIR:${target}>"
        COMMAND_EXPAND_LISTS VERBATIM
        COMMENT "Staging assets, scripts and runtime DLLs for ${target}")
endfunction()

add_subdirectory(Engine)
add_subdirectory(Editor)
add_subdirectory(Game)
add_subdirectory(Tools/VoxelProbe)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Editor)
