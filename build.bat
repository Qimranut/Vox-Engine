@echo off
REM ==========================================================================
REM  Generate the Visual Studio solution and build one configuration.
REM
REM      build.bat            Debug   (asserts, GL debug output, slow terrain)
REM      build.bat release    Release (what to edit levels and profile in)
REM
REM  The solution in build\ is GENERATED OUTPUT - never edit project settings
REM  through the VS GUI, they are discarded on the next regeneration.
REM
REM  VOX_NO_PAUSE=1 skips the pauses, for scripts that call this one.
REM ==========================================================================

set "VOX_PRESET=debug"
set "VOX_CONFIG=Debug"

if /i "%~1"=="release" (
    set "VOX_PRESET=release"
    set "VOX_CONFIG=Release"
) else if not "%~1"=="" if /i not "%~1"=="debug" (
    echo Unknown configuration "%~1". Use: build.bat [debug^|release]
    exit /b 1
)

cmake --preset vs2022
if %errorlevel% neq 0 (
    echo.
    echo CMake generation failed.
    if not defined VOX_NO_PAUSE pause
    exit /b 1
)

cmake --build --preset %VOX_PRESET%
if %errorlevel% neq 0 (
    echo.
    echo Build failed.
    if not defined VOX_NO_PAUSE pause
    exit /b 1
)

echo.
echo Build successful (%VOX_CONFIG%).
echo   Solution: build\voxEngine.sln
echo   Output:   build\bin\%VOX_CONFIG%\Editor.exe
if not defined VOX_NO_PAUSE pause
