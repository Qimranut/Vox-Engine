# FreeType is used only by this offline tool, never by Engine/Editor/Game.
foreach(feature ZLIB BZIP2 PNG HARFBUZZ)
    set(FT_WITH_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(package ZLIB BZip2 PNG HarfBuzz)
    set(CMAKE_DISABLE_FIND_PACKAGE_${package} TRUE)
endforeach()
# This FreeType drop declares cmake_minimum_required(VERSION 2.8.12), which
# CMake 4 rejects. Scoped to this directory, so no other target is affected.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
add_subdirectory("${PROJECT_SOURCE_DIR}/Vendor/freetype" "${CMAKE_CURRENT_BINARY_DIR}/freetype" EXCLUDE_FROM_ALL)
target_compile_options(freetype PRIVATE /W0)
add_executable(FontCompiler main.cpp)
target_include_directories(FontCompiler PRIVATE "${PROJECT_SOURCE_DIR}/Engine/Source")
target_link_libraries(FontCompiler PRIVATE freetype)
set_target_properties(FontCompiler freetype PROPERTIES FOLDER "Tools")
