add_executable(AudioCompiler main.cpp)
target_include_directories(AudioCompiler PRIVATE "${PROJECT_SOURCE_DIR}/Engine/Source")
target_link_libraries(AudioCompiler PRIVATE vendor_fmod)
set_target_properties(AudioCompiler PROPERTIES FOLDER "Tools"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VS_DEBUGGER_COMMAND_ARGUMENTS "Assets")
add_custom_command(TARGET AudioCompiler POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:AudioCompiler> "$<TARGET_FILE_DIR:AudioCompiler>"
    COMMAND_EXPAND_LISTS VERBATIM)
