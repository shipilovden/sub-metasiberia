@echo off
REM Улучшенный скрипт для запуска GUI клиента Substrata с параметрами совместимости

echo Запуск GUI клиента Substrata с параметрами совместимости...

set GUI_PATH=C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe

if not exist "%GUI_PATH%" (
    echo ОШИБКА: GUI клиент не найден!
    echo Путь: %GUI_PATH%
    echo.
    echo Возможные решения:
    echo 1. Запустите build_metasiberia.bat для сборки проекта
    echo 2. Проверьте, что сборка завершилась успешно
    echo.
    pause
    exit /b 1
)

echo GUI клиент найден: %GUI_PATH%
echo Запуск с параметрами совместимости...
echo.

cd /d "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo"

REM Запуск с параметрами для решения проблем OpenGL
start "" "gui_client.exe" -u sub://vr.metasiberia.com --no_bindless --no_MDI

echo GUI клиент запущен с параметрами:
echo - URL: sub://vr.metasiberia.com
echo - --no_bindless (отключение bindless textures)
echo - --no_MDI (отключение Multi-Draw Indirect)
echo.
echo Если проблемы продолжаются, попробуйте обновить драйверы видеокарты.
echo.
