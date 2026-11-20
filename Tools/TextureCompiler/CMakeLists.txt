# ============================================================================
#  TextureCompiler - the offline texture cooker.
#
#  Compresses every source image under Assets/ into a BCn DDS with a full mip
#  chain, written to Assets/.cooked/<same path>.dds. The runtime prefers that
#  file over the source (vox::ResourceManager::read). Built from DirectXTex;
#  it links nothing from Engine.
# ============================================================================

# BC6H/BC7 encoding is a brute-force search. Unoptimised, a Debug build cooks
# one 1024x1024 texture in minutes, so this directory compiles Debug with /O2
# too. /RTC1 cannot be combined with /O2. The CRT stays /MDd.
string(REPLACE "/Od" "/O2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
string(REPLACE "/Ob0" "/Ob2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

set(TEXTURE_COMPILER_SOURCES
    main.cpp
    texture_compiler.cpp texture_compiler.hpp
    texture_settings.cpp texture_settings.hpp)

add_executable(TextureCompiler ${TEXTURE_COMPILER_SOURCES})
target_link_libraries(TextureCompiler PRIVATE vendor_directxtex ole32 windowscodecs)
target_compile_definitions(TextureCompiler PRIVATE UNICODE _UNICODE)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "TextureCompiler" FILES ${TEXTURE_COMPILER_SOURCES})
set_target_properties(TextureCompiler PROPERTIES
    FOLDER "Tools"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VS_DEBUGGER_COMMAND_ARGUMENTS "Assets")
