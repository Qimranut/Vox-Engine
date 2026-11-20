# ============================================================================
#  VoxelProbe - generates a voxel map from its recipe without a window.
#
#  Runs the Engine's voxel generator, mipper, mesher and .voxmap writer, prints
#  timings and terrain statistics, and writes a hillshaded PNG preview. It is a
#  tool, so stb_image_write (tools-only under the approved-library list) is
#  linked here and never by Engine. Like the Editor, it reaches into
#  Engine/Source for the voxel and resource internals.
# ============================================================================

add_executable(VoxelProbe main.cpp)
target_include_directories(VoxelProbe PRIVATE "${PROJECT_SOURCE_DIR}/Engine/Source")
# vendor_tracy: Engine/Source headers change layout with the profiler defines,
# so every target that includes them has to see the same ones as Engine.
target_link_libraries(VoxelProbe PRIVATE Engine vendor_glm vendor_stb vendor_tracy)
set_target_properties(VoxelProbe PROPERTIES
    FOLDER "Tools"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VS_DEBUGGER_COMMAND_ARGUMENTS "Assets")
