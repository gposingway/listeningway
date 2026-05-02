@echo off
setlocal

set RESHADE_SDK_PATH=%CD%\third_party\reshade\include

REM === Generate ListeningwayUniforms.fxh from template ===
REM NUM_BANDS is extracted from DEFAULT_NUM_BANDS in src\core\constants.h —
REM the single source of truth. It defines the compile-time size of the
REM Listeningway_FreqBands[] array in shaders and is also the runtime
REM default band count. The live band count the addon is currently
REM publishing is exposed via the Listeningway_NumBands uniform so shaders
REM iterate only populated entries.
set CONSTANTS_H=src\core\constants.h
if not exist %CONSTANTS_H% (
    echo Constants file %CONSTANTS_H% not found!
    exit /b 1
)
set NB_TMP=%TEMP%\lw_num_bands.txt
powershell -NoProfile -Command "$m = Get-Content '%CONSTANTS_H%' | Select-String 'constexpr\s+size_t\s+DEFAULT_NUM_BANDS\s*=\s*(\d+)' | Select-Object -First 1; if ($m) { $m.Matches.Groups[1].Value } else { '' }" > %NB_TMP%
set /p NUM_BANDS=<%NB_TMP%
del %NB_TMP%
if "%NUM_BANDS%"=="" (
    echo Could not extract DEFAULT_NUM_BANDS from %CONSTANTS_H%!
    exit /b 1
)
echo Using DEFAULT_NUM_BANDS = %NUM_BANDS% from %CONSTANTS_H%
set TEMPLATE=templates\ListeningwayUniforms.fxh.template
set OUTPUT=assets\ListeningwayUniforms.fxh
if exist %TEMPLATE% (
    powershell -Command "(Get-Content %TEMPLATE%) -replace '\{\{NUM_BANDS\}\}', '%NUM_BANDS%' | Set-Content %OUTPUT%"
) else (
    echo Template %TEMPLATE% not found!
    exit /b 1
)

REM === Generate listeningway.rc from template and current-version.txt ===
set RC_TEMPLATE=templates\listeningway.rc.template
set RC_OUTPUT=assets\listeningway.rc
set VERSION_FILE=current-version.txt
if not exist %RC_TEMPLATE% (
    echo Template %RC_TEMPLATE% not found!
    exit /b 1
)
if not exist %VERSION_FILE% (
    echo Version file %VERSION_FILE% not found!
    exit /b 1
)
for /f "usebackq delims=" %%v in (%VERSION_FILE%) do set VERSION_DOT=%%v
set VERSION_COMMA=%VERSION_DOT:.=,%
REM Replace placeholders in template
powershell -Command "(Get-Content %RC_TEMPLATE%) -replace '\{\{VERSION_DOT\}\}', '%VERSION_DOT%' -replace '\{\{VERSION_COMMA\}\}', '%VERSION_COMMA%' | Set-Content %RC_OUTPUT%"

REM Build the project using MSBuild
if not exist build (
    echo Build directory not found. Run prepare.bat first.
    exit /b 1
)

REM Use Release config by default
set CONFIG=Release
if not "%1"=="" set CONFIG=%1

REM Build with MSBuild
msbuild build\Listeningway.sln /p:Configuration=%CONFIG% /m
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

REM Rename and move the output .dll to .addon in dist
set OUTDLL=build\%CONFIG%\Listeningway.dll
set OUTADDON=build\%CONFIG%\Listeningway.addon
set DIST=dist
if exist %OUTADDON% del %OUTADDON%
if exist %OUTDLL% ren %OUTDLL% Listeningway.addon
if not exist %DIST% mkdir %DIST%
move /Y build\%CONFIG%\Listeningway.addon %DIST%\Listeningway.addon

REM Copy Listeningway.fx to dist
copy /Y assets\Listeningway.fx %DIST%\Listeningway.fx
REM Copy ListeningwayUniforms.fxh to dist
copy /Y assets\ListeningwayUniforms.fxh %DIST%\ListeningwayUniforms.fxh

REM Extract FileVersion from listeningway.rc and write to dist/VERSION.txt using PowerShell (robust to spaces)
powershell -Command "Select-String 'FileVersion' assets/listeningway.rc | Select-Object -ExpandProperty Line | Where-Object { $_ -like '*VALUE*' } | ForEach-Object { if ($_ -match '"([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)"') { $matches[1] } }" > %DIST%\VERSION.txt

REM No DLLs to copy for static build

echo --- Build, Rename, and Move Successful ---

REM Optionally deploy to LISTENINGWAY_DEPLOY_DIR. The deploy step is opt-in:
REM set LISTENINGWAY_DEPLOY_DIR to a target directory (e.g. a game install) and
REM build.bat will copy the built artifacts there. If the variable is unset,
REM the deploy step is skipped silently.
if exist .\deploy.bat (
    if defined LISTENINGWAY_DEPLOY_DIR (
        echo.
        echo --- Automatically deploying to %LISTENINGWAY_DEPLOY_DIR% ---
        call .\deploy.bat
    )
)

endlocal