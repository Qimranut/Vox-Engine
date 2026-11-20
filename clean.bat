@echo off
REM ==========================================================================
REM  Delete the generated build tree.
REM  When CMake's cache gets into a weird state - usually after moving the
REM  folder, switching generator, or a half-finished configure - deleting
REM  build\ and regenerating fixes about 90%% of it.
REM ==========================================================================

if exist build (
    echo Deleting build\ ...
    rmdir /s /q build
    echo Done. Run build.bat to regenerate.
) else (
    echo Nothing to clean - build\ does not exist.
)
pause
