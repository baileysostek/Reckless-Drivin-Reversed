@echo off
setlocal enabledelayedexpansion

:: Remove trailing backslash from script dir
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "BUILD_DIR=%SCRIPT_DIR%\build"
set "SDL2_DIR=%SCRIPT_DIR%\SDL2"
set "SDL2_VERSION=2.30.11"

:: Find cmake
set "CMAKE=cmake"
where cmake >nul 2>&1
if !errorlevel! neq 0 (
    if exist "C:\Program Files\CMake\bin\cmake.exe" (
        set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
    ) else (
        echo ERROR: cmake not found. Install CMake and add it to PATH.
        exit /b 1
    )
)

:: Download SDL2 source if not present
if not exist "%SDL2_DIR%\CMakeLists.txt" (
    echo === Downloading SDL2 %SDL2_VERSION% source ===
    where curl >nul 2>&1
    if !errorlevel! neq 0 (
        echo ERROR: curl not found. Please download SDL2 source manually into %SDL2_DIR%
        exit /b 1
    )
    curl -L "https://github.com/libsdl-org/SDL/releases/download/release-%SDL2_VERSION%/SDL2-%SDL2_VERSION%.tar.gz" -o "%TEMP%\SDL2.tar.gz"
    tar xzf "%TEMP%\SDL2.tar.gz" -C "%SCRIPT_DIR%"
    move "%SCRIPT_DIR%\SDL2-%SDL2_VERSION%" "%SDL2_DIR%"
    del "%TEMP%\SDL2.tar.gz"
)

echo === Configuring (Debug) ===
"%CMAKE%" -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Debug
if !errorlevel! neq 0 (
    echo ERROR: CMake configure failed.
    exit /b 1
)

echo === Building (Debug) ===
"%CMAKE%" --build "%BUILD_DIR%" --config Debug
if !errorlevel! neq 0 (
    echo ERROR: Build failed.
    exit /b 1
)

echo === Extracting resources ===
"%CMAKE%" --build "%BUILD_DIR%" --target extract_resources

echo === Done ===
echo Output: %BUILD_DIR%\Debug\RecklessDrivin.exe
