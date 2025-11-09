@echo off
echo ========================================
echo Building shki-nvkz Custom SDL Client
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
REM Build shki-nvkz Custom SDL Client
REM ========================================
echo.
echo ========================================
echo Building shki-nvkz Custom SDL Client...
echo ========================================

cd /d C:\programming\substrata
if not exist C:\programming\substrata_build_sdl_custom mkdir C:\programming\substrata_build_sdl_custom
cd C:\programming\substrata_build_sdl_custom

echo Configuring shki-nvkz build...
cmake C:\programming\substrata -G "Visual Studio 17 2022" -A x64 ^
    -DUSE_SDL=ON ^
    -DCUSTOM_BUILD=ON ^
    -DINDIGO_SUPPORT=OFF ^
    -DCEF_SUPPORT=OFF

if %ERRORLEVEL% neq 0 (
    echo ERROR: shki-nvkz CMake configuration failed!
    pause
    exit /b 1
)

echo Building shki-nvkz client...
cmake --build . --config RelWithDebInfo --target gui_client

if %ERRORLEVEL% neq 0 (
    echo ERROR: shki-nvkz build failed!
    pause
    exit /b 1
)

echo shki-nvkz Client build completed.
echo.

REM ========================================
REM Copy Resources for shki-nvkz
REM ========================================
echo Copying resources for shki-nvkz client...

REM Copy common resources
xcopy /s /e /y "%GLARE_CORE_TRUNK_DIR%\resources" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\data\resources\"
xcopy /s /e /y "%GLARE_CORE_TRUNK_DIR%\shaders" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\data\shaders\"
xcopy /s /e /y "%GLARE_CORE_TRUNK_DIR%\webserver_public_files" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\data\webserver_public_files\"
xcopy /s /e /y "%GLARE_CORE_TRUNK_DIR%\webserver_fragments" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\data\webserver_fragments\"
xcopy /s /e /y "%GLARE_CORE_TRUNK_DIR%\server_dist_resources" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\data\server_dist_resources\"

REM Copy shki-nvkz specific resources
echo Copying shki-nvkz specific resources...
xcopy /y "C:\programming\substrata\resources\shki-nvkz\buttons\shki-nvkz_avatar.png" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\data\resources\buttons\"
xcopy /y "C:\programming\substrata\resources\shki-nvkz\buttons\shki-nvkz_minimap_icom.png" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\data\resources\buttons\"
xcopy /y "C:\programming\substrata\resources\shki-nvkz\buttons\shki-nvkz_expand_chat_icon.png" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\data\resources\buttons\"
xcopy /y "C:\programming\substrata\resources\shki-nvkz\translations\metasiberia_ru.ts" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\data\resources\translations\"

REM Apply custom icon using rcedit.exe
echo Applying custom icon to shki-nvkz.exe...
copy "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\shki-nvkz.exe" "%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\shki-nvkz.exe.backup"
powershell -Command "Start-Process -FilePath 'C:\programming\rcedit.exe' -ArgumentList 'shki-nvkz.exe --set-icon C:\programming\substrata\resources\shki-nvkz\icons\shki-nvkz.ico' -Verb RunAs -WorkingDirectory '%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo'"

if %ERRORLEVEL% neq 0 (
    echo WARNING: rcedit.exe may have failed, but continuing...
)

echo Custom icon applied.
echo.

REM Clear Windows icon cache
echo Clearing Windows icon cache...
powershell -Command "Remove-ItemProperty -Path 'HKCU:\Software\Classes\Local Settings\Software\Microsoft\Windows\Shell\MuiCache' -Name '*shki-nvkz.exe*' -ErrorAction SilentlyContinue"
powershell -Command "Stop-Process -Name explorer -Force; Start-Process explorer"

echo.
echo ========================================
echo shki-nvkz build completed successfully!
echo ========================================
echo.
echo Output directory: %CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\
echo Executable: shki-nvkz.exe
echo.
pause

