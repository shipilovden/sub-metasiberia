@echo off
echo ========================================
echo Metasiberia Triple Client Build System
echo ========================================
echo.

REM Set environment variables
set CYBERSPACE_OUTPUT=C:\programming\substrata_output
set GLARE_CORE_LIBS=C:\programming\glare-core
set GLARE_CORE_TRUNK_DIR=C:\programming\glare-core
set WINTER_DIR=C:\programming\winter

echo Environment variables set:
echo CYBERSPACE_OUTPUT=%CYBERSPACE_OUTPUT%
echo GLARE_CORE_LIBS=%GLARE_CORE_LIBS%
echo GLARE_CORE_TRUNK_DIR=%GLARE_CORE_TRUNK_DIR%
echo WINTER_DIR=%WINTER_DIR%
echo.

REM ========================================
REM Build 1: Qt Client (Default)
REM ========================================
echo.
echo ========================================
echo Building Qt Client...
echo ========================================

cd /d C:\programming\substrata
if not exist substrata_build_qt mkdir substrata_build_qt
cd substrata_build_qt

echo Configuring Qt build...
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DUSE_SDL=OFF ^
    -DCUSTOM_BUILD=OFF ^
    -DINDIGO_SUPPORT=OFF ^
    -DCEF_SUPPORT=OFF

if %ERRORLEVEL% neq 0 (
    echo ERROR: Qt CMake configuration failed!
    pause
    exit /b 1
)

echo Building Qt client...
cmake --build . --config RelWithDebInfo --target gui_client

if %ERRORLEVEL% neq 0 (
    echo ERROR: Qt build failed!
    pause
    exit /b 1
)

echo Qt build completed successfully!
echo.

REM ========================================
REM Build 2: SDL Client (Standard)
REM ========================================
echo.
echo ========================================
echo Building SDL Client (Standard)...
echo ========================================

cd /d C:\programming\substrata
if not exist substrata_build_sdl mkdir substrata_build_sdl
cd substrata_build_sdl

echo Configuring SDL build...
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DUSE_SDL=ON ^
    -DCUSTOM_BUILD=OFF ^
    -DINDIGO_SUPPORT=OFF ^
    -DCEF_SUPPORT=OFF

if %ERRORLEVEL% neq 0 (
    echo ERROR: SDL CMake configuration failed!
    pause
    exit /b 1
)

echo Building SDL client...
cmake --build . --config RelWithDebInfo --target gui_client

if %ERRORLEVEL% neq 0 (
    echo ERROR: SDL build failed!
    pause
    exit /b 1
)

echo SDL build completed successfully!
echo.

REM ========================================
REM Build 3: SDL Client (Custom - shki-nvkz)
REM ========================================
echo.
echo ========================================
echo Building SDL Client (Custom - shki-nvkz)...
echo ========================================

cd /d C:\programming\substrata
if not exist substrata_build_sdl_custom mkdir substrata_build_sdl_custom
cd substrata_build_sdl_custom

echo Configuring Custom SDL build...
echo Copying custom resource file...
if not exist gui_client mkdir gui_client
copy ..\gui_client\shki-nvkz.rc gui_client\shki-nvkz.rc >nul 2>&1

cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DUSE_SDL=ON ^
    -DCUSTOM_BUILD=ON ^
    -DINDIGO_SUPPORT=OFF ^
    -DCEF_SUPPORT=OFF

if %ERRORLEVEL% neq 0 (
    echo ERROR: Custom SDL CMake configuration failed!
    pause
    exit /b 1
)

echo Building Custom SDL client...
cmake --build . --config RelWithDebInfo --target gui_client

if %ERRORLEVEL% neq 0 (
    echo ERROR: Custom SDL build failed!
    pause
    exit /b 1
)

echo Custom SDL build completed successfully!
echo.

REM ========================================
REM Copy resources and DLLs
REM ========================================
echo.
echo ========================================
echo Copying resources and DLLs...
echo ========================================

REM Copy resources to SDL builds
echo Copying resources to SDL builds...
xcopy /Y /E "C:\programming\substrata\resources" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl\RelWithDebInfo\resources\"
xcopy /Y /E "C:\programming\substrata\shaders" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl\RelWithDebInfo\shaders\"
xcopy /Y /E "C:\programming\substrata\webserver_public_files" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl\RelWithDebInfo\webserver_public_files\"

REM Copy resources to Custom SDL build
echo Copying resources to Custom SDL build...
xcopy /Y /E "C:\programming\substrata\resources" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\resources\"
xcopy /Y /E "C:\programming\substrata\shaders" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\shaders\"
xcopy /Y /E "C:\programming\substrata\webserver_public_files" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\webserver_public_files\"

REM Replace custom icons for shki-nvkz build
echo Applying custom icons for shki-nvkz build...
REM Using standard chat icon (same as regular SDL build)
copy "C:\programming\substrata\resources\buttons\shki-nvkz_avatar.png" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\data\resources\buttons\avatar.png"
copy "C:\programming\substrata\resources\buttons\shki-nvkz_minimap_icom.png" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\data\resources\buttons\minimap_icon.png"

REM ========================================
REM Copy DLLs and finalize
REM ========================================
echo.
echo ========================================
echo Copying DLLs and finalizing...
echo ========================================

REM Copy DLLs to SDL builds
echo Copying DLLs to SDL builds...

REM Copy from Qt build to SDL builds
set QT_BUILD_DIR=%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64\RelWithDebInfo
set SDL_BUILD_DIR=%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl\RelWithDebInfo
set CUSTOM_BUILD_DIR=%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo

if exist "%QT_BUILD_DIR%" (
    echo Copying DLLs to standard SDL build...
    copy "%QT_BUILD_DIR%\*.dll" "%SDL_BUILD_DIR%\" >nul 2>&1
    copy "%QT_BUILD_DIR%\server.exe" "%SDL_BUILD_DIR%\" >nul 2>&1
    
    echo Copying DLLs to custom SDL build...
    copy "%QT_BUILD_DIR%\*.dll" "%CUSTOM_BUILD_DIR%\" >nul 2>&1
    copy "%QT_BUILD_DIR%\server.exe" "%CUSTOM_BUILD_DIR%\" >nul 2>&1
    
    echo DLLs copied successfully!
) else (
    echo WARNING: Qt build directory not found, skipping DLL copy
)

REM ========================================
REM Final Summary
REM ========================================
echo.
echo ========================================
echo BUILD COMPLETED SUCCESSFULLY!
echo ========================================
echo.
echo Executables created:
echo.
echo 1. Qt Client:
echo    %CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64\RelWithDebInfo\Metasiberia_v1.0.0.exe
echo.
echo 2. SDL Client (Standard):
echo    %CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl\RelWithDebInfo\Metasiberia SDL.exe
echo.
echo 3. SDL Client (Custom - shki-nvkz):
echo    %CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\shki-nvkz.exe
echo.

echo Applying custom icon to shki-nvkz.exe...
if exist "C:\programming\rcedit.exe" (
    "C:\programming\rcedit.exe" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\shki-nvkz.exe" --set-icon "C:\programming\substrata\icons\shki-nvkz.ico"
    echo Custom icon applied successfully!
) else (
    echo Warning: rcedit.exe not found. Custom icon may not be applied.
    echo Download rcedit.exe from: https://github.com/electron/rcedit/releases
)

echo.
echo ========================================
echo All three builds completed successfully!
echo ========================================
echo.
pause
