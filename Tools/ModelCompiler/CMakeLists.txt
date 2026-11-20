# ============================================================================
#  ModelCompiler - the offline model cooker.
#
#  Imports every source model under Assets/ with Assimp and writes the
#  runtime's .vmdl format to Assets/.cooked/<same path>.vmdl. Assimp is
#  tools-only under the approved-library list, so this is the only target that
#  links it; vox::ResourceManager reads the .vmdl. It links nothing from Engine.
# ============================================================================

set(MODEL_COMPILER_SOURCES
    main.cpp
    model_compiler.cpp model_compiler.hpp)

add_executable(ModelCompiler ${MODEL_COMPILER_SOURCES})
target_link_libraries(ModelCompiler PRIVATE assimp)
target_compile_definitions(ModelCompiler PRIVATE UNICODE _UNICODE)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "ModelCompiler" FILES ${MODEL_COMPILER_SOURCES})
set_target_properties(ModelCompiler PROPERTIES
    FOLDER "Tools"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VS_DEBUGGER_COMMAND_ARGUMENTS "Assets")
