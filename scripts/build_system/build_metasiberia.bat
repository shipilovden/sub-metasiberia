@echo off
REM Скрипт для сборки проекта Metasiberia
REM Автоматически настраивает переменные окружения и собирает проект

echo ========================================
echo Сборка проекта Metasiberia
echo ========================================

REM Установка переменных окружения
set GLARE_CORE_LIBS=C:\programming
set CYBERSPACE_OUTPUT=C:\programming\substrata_output
set GLARE_CORE_TRUNK_DIR=C:\programming\glare-core
set WINTER_DIR=C:\programming\winter

echo Переменные окружения установлены:
echo GLARE_CORE_LIBS=%GLARE_CORE_LIBS%
echo CYBERSPACE_OUTPUT=%CYBERSPACE_OUTPUT%
echo GLARE_CORE_TRUNK_DIR=%GLARE_CORE_TRUNK_DIR%
echo WINTER_DIR=%WINTER_DIR%
echo.

REM Переход в директорию сборки
cd /d C:\programming\substrata_build_qt

REM Очистка предыдущей конфигурации
echo Очистка предыдущей конфигурации...
if exist CMakeCache.txt del CMakeCache.txt
if exist CMakeFiles rmdir /s /q CMakeFiles

REM Конфигурация проекта
echo Конфигурация проекта...
cmake C:\programming\substrata -G "Visual Studio 17 2022" -A x64 -DBUGSPLAT_SUPPORT=OFF -DCEF_SUPPORT=OFF -DSUBSTRATA_VERSION=1.0.0

if %ERRORLEVEL% neq 0 (
    echo ОШИБКА: Конфигурация проекта не удалась!
    pause
    exit /b 1
)

REM Сборка проекта
echo Сборка проекта...
cmake --build . --config RelWithDebInfo --target gui_client

if %ERRORLEVEL% neq 0 (
    echo ОШИБКА: Сборка проекта не удалась!
    pause
    exit /b 1
)

REM ================== Переводы (.qm) ==================
REM Путь к исходному .ts
set TS_FILE=C:\programming\substrata\resources\translations\metasiberia_ru.ts
REM Путь назначения рядом с exe
set EXE_DIR=%CYBERSPACE_OUTPUT%\vs2022\cyberspace_x64\RelWithDebInfo
set DEST_TRANSL_DIR=%EXE_DIR%\translations
set DEST_QM=%DEST_TRANSL_DIR%\metasiberia_ru.qm

REM Ищем lrelease.exe: 1) из переменной INDIGO_QT_DIR  2) стандартные пути Qt, если заданы через QT_DIR  3) локальный Qt в C:\programming\Qt
set LRELEASE=
if exist "%INDIGO_QT_DIR%\bin\lrelease.exe" set LRELEASE=%INDIGO_QT_DIR%\bin\lrelease.exe
if not defined LRELEASE if exist "%QT_DIR%\bin\lrelease.exe" set LRELEASE=%QT_DIR%\bin\lrelease.exe
if not defined LRELEASE if exist "C:\programming\Qt\5.15.2\msvc2019_64\bin\lrelease.exe" set LRELEASE=C:\programming\Qt\5.15.2\msvc2019_64\bin\lrelease.exe
if not defined LRELEASE if exist "C:\programming\Qt\6.5.0\msvc2019_64\bin\lrelease.exe" set LRELEASE=C:\programming\Qt\6.5.0\msvc2019_64\bin\lrelease.exe

echo.
echo Обработка переводов...
if not exist "%DEST_TRANSL_DIR%" mkdir "%DEST_TRANSL_DIR%"

REM Если lrelease найден, сгенерируем .qm из .ts
if defined LRELEASE (
    echo Найден lrelease: %LRELEASE%
    echo Генерация metasiberia_ru.qm...
    "%LRELEASE%" -silent "%TS_FILE%" -qm "%DEST_QM%"
) else (
    echo ВНИМАНИЕ: lrelease.exe не найден. Попытаемся скопировать уже готовый .qm, если он есть.
    REM Пробуем скопировать предсобранный .qm из исходников
    if exist "C:\programming\substrata\resources\translations\metasiberia_ru.qm" copy /Y "C:\programming\substrata\resources\translations\metasiberia_ru.qm" "%DEST_QM%" >nul
)

REM Если после всех попыток файла все еще нет - предупреждаем
if not exist "%DEST_QM%" (
    echo ВНИМАНИЕ: файл перевода metasiberia_ru.qm не найден/не создан. Русский язык может не включаться.
) else (
    echo Перевод metasiberia_ru.qm готов: %DEST_QM%
)

echo ========================================
echo Сборка завершена успешно!
echo GUI клиент: C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe
echo ========================================
pause
