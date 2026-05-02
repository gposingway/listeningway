@echo off
setlocal enabledelayedexpansion

echo --- Listeningway Deployment Script ---

REM Source dir is always `dist/` next to this script.
set "SOURCE_DIR=%~dp0dist"
set "ADDON_FILE=Listeningway.addon"

REM Target dir is read from LISTENINGWAY_DEPLOY_DIR. This is typically a game
REM install (e.g. "E:\Games\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game")
REM but is left to the user since it's machine-specific.
if not defined LISTENINGWAY_DEPLOY_DIR (
    echo LISTENINGWAY_DEPLOY_DIR is not set; skipping deploy.
    echo To enable: set LISTENINGWAY_DEPLOY_DIR=^<path-to-target-game-or-app^>
    endlocal
    exit /b 0
)
set "TARGET_DIR=%LISTENINGWAY_DEPLOY_DIR%"

if not exist "%SOURCE_DIR%\%ADDON_FILE%" (
    echo ERROR: Source file %SOURCE_DIR%\%ADDON_FILE% not found.
    echo Make sure you've built the project successfully before running this script.
    endlocal
    exit /b 1
)

if not exist "%TARGET_DIR%" (
    echo ERROR: Target directory not found: %TARGET_DIR%
    echo Set LISTENINGWAY_DEPLOY_DIR to an existing path or create it manually.
    endlocal
    exit /b 1
)

REM Ensure ReShade shader directory exists.
set "SHADER_DIR=%TARGET_DIR%\reshade-shaders\Shaders"
if not exist "%SHADER_DIR%" (
    echo Creating ReShade shader directory: %SHADER_DIR%
    mkdir "%SHADER_DIR%"
    if !errorlevel! neq 0 (
        echo ERROR: Failed to create shader directory.
        endlocal
        exit /b 1
    )
)

echo Copying %ADDON_FILE% to %TARGET_DIR%...
copy /Y "%SOURCE_DIR%\%ADDON_FILE%" "%TARGET_DIR%\" >nul
if !errorlevel! neq 0 (
    echo WARNING: Failed to copy %ADDON_FILE%.
)

if exist "%SOURCE_DIR%\Listeningway.fx" (
    copy /Y "%SOURCE_DIR%\Listeningway.fx" "%SHADER_DIR%\Listeningway.fx" >nul || echo WARNING: Failed to copy Listeningway.fx.
)
if exist "%SOURCE_DIR%\ListeningwayUniforms.fxh" (
    copy /Y "%SOURCE_DIR%\ListeningwayUniforms.fxh" "%SHADER_DIR%\ListeningwayUniforms.fxh" >nul || echo WARNING: Failed to copy ListeningwayUniforms.fxh.
)

echo.
echo Listeningway successfully deployed to:
echo %TARGET_DIR%\%ADDON_FILE%
echo.
echo --- Deployment Complete ---

endlocal
