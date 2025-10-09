@echo off
REM Скрипт для запуска GUI клиента Substrata

echo Запуск GUI клиента Substrata...

set GUI_PATH=C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe

if not exist "%GUI_PATH%" (
    echo ОШИБКА: GUI клиент не найден!
    echo Путь: %GUI_PATH%
    echo.
    echo Возможные решения:
    echo 1. Запустите build_substrata.bat для сборки проекта
    echo 2. Проверьте, что сборка завершилась успешно
    echo.
    pause
    exit /b 1
)

echo GUI клиент найден: %GUI_PATH%
echo Запуск...
echo.

cd /d "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo"

REM Запуск с параметрами для решения проблем OpenGL
start "" "gui_client.exe" -u sub://vr.metasiberia.com --no_bindless --no_MDI

echo GUI клиент запущен с параметрами совместимости!
echo URL: sub://vr.metasiberia.com
echo Параметры: --no_bindless --no_MDI
