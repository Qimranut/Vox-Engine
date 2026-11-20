set(EDITOR_SOURCES
    main.cpp
    Source/app/EditorGizmo.cpp
    Source/app/EditorUI.cpp Source/app/EditorUI.hpp
    Source/app/EditorUICommon.hpp
    Source/app/EditorUIChrome.cpp
    Source/app/EditorUIDialogs.cpp
    Source/app/EditorUILevel.cpp
    Source/app/EditorUIPanels.cpp
    Source/app/EditorUITerrain.cpp
    Source/app/EditorUIViewport.cpp
    Source/app/ContentBrowser.cpp Source/app/ContentBrowser.hpp
    Source/app/LogConsole.cpp Source/app/LogConsole.hpp
    Source/app/ProfilerPanel.cpp Source/app/ProfilerPanel.hpp
    Source/app/PipelinePanel.cpp Source/app/PipelinePanel.hpp
    Source/app/FileWatcher.cpp Source/app/FileWatcher.hpp
    Source/app/ShellOpen.cpp Source/app/ShellOpen.hpp
    Source/app/terrain/PipelinePreview.cpp Source/app/terrain/PipelinePreview.hpp
    Source/app/TerrainPanel.cpp Source/app/TerrainPanel.hpp
    Source/app/gizmo/RegionGizmo.cpp Source/app/gizmo/RegionGizmo.hpp
    Source/app/gizmo/RotateRings.cpp Source/app/gizmo/RotateRings.hpp
    Source/app/gizmo/OrientedBoxGizmo.cpp Source/app/gizmo/OrientedBoxGizmo.hpp
    Source/app/terrain/TransformTool.cpp Source/app/terrain/TransformTool.hpp
    Source/app/terrain/BrushSettings.hpp
    Source/app/terrain/BrushStroke.cpp Source/app/terrain/BrushStroke.hpp
    Source/app/terrain/BrushTool.cpp Source/app/terrain/BrushTool.hpp
    Source/app/terrain/TerrainTool.hpp
    Source/app/terrain/TerrainToolset.cpp Source/app/terrain/TerrainToolset.hpp
    Source/app/terrain/ToolWidgets.cpp Source/app/terrain/ToolWidgets.hpp
    Source/app/EditorSettings.cpp Source/app/EditorSettings.hpp)
vox_application(Editor ${EDITOR_SOURCES})
# Editor consumes the scene/resource APIs, which expose GLM and EnTT types.
target_include_directories(Editor PRIVATE "${PROJECT_SOURCE_DIR}/Engine/Source")
# shell32: the Content Browser's "Show in Explorer".
# vendor_tracy: the Editor instruments its own UI through Engine/Source/profile, and the engine
# headers it includes change layout with the profiler defines, so it must see the same ones.
target_link_libraries(Editor PRIVATE vendor_imgui vendor_imguizmo vendor_glew vendor_glm vendor_entt vendor_tracy shell32)
target_compile_options(Editor PRIVATE /bigobj)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Editor" FILES ${EDITOR_SOURCES})
