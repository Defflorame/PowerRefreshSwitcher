@echo off
setlocal EnableExtensions EnableDelayedExpansion

pushd "%~dp0"
set "NO_PAUSE=0"
if /i "%~1"=="--no-pause" set "NO_PAUSE=1"
set "PF86=%ProgramFiles(x86)%"

where cl.exe >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=!PF86!\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo Visual Studio Build Tools were not found.
        echo Install "Desktop development with C++" in Visual Studio Installer.
        if "%NO_PAUSE%"=="0" pause
        exit /b 1
    )

    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"

    if not defined VSINSTALL (
        echo MSVC C++ tools were not found.
        echo Open Visual Studio Installer and add "Desktop development with C++".
        if "%NO_PAUSE%"=="0" pause
        exit /b 1
    )

    if not exist "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" (
        echo vcvars64.bat was not found:
        echo "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat"
        if "%NO_PAUSE%"=="0" pause
        exit /b 1
    )

    call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat"
)

cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /Fe:PowerRefreshSwitcher.exe PowerRefreshSwitcher.cpp user32.lib /link /SUBSYSTEM:WINDOWS
if errorlevel 1 (
    echo.
    echo Build failed.
    if "%NO_PAUSE%"=="0" pause
    exit /b 1
)

echo.
echo Build succeeded: %CD%\PowerRefreshSwitcher.exe
echo.
echo To add autostart, run:
echo powershell -ExecutionPolicy Bypass -File .\install-startup.ps1
echo.
if "%NO_PAUSE%"=="0" pause
