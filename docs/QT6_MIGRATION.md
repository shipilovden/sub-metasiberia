# Миграция на Qt 6 - Инструкция

## Текущий статус

Проект поддерживает **обе версии Qt** (5.15.16 и 6.10.0) через условную компиляцию.

## Как переключиться на Qt 6

### 1. Установка Qt 6.10.0

Установите Qt 6.10.0 через Qt Maintenance Tool:
- Запустите `install_qt6_msvc.bat` или вручную через `C:\Qt\MaintenanceTool.exe`
- Установите: **Qt 6.10.0 → MSVC 2022 64-bit**
- Обязательные модули:
  - Qt Core
  - Qt Gui
  - Qt OpenGL
  - Qt Widgets
  - Qt OpenGLWidgets
  - **Qt Core5Compat** (важно для совместимости!)

### 2. Настройка переменной окружения

**Windows (PowerShell):**
```powershell
$env:QT_VERSION = "6.10.0"
```

**Windows (CMD):**
```cmd
set QT_VERSION=6.10.0
```

**Windows (постоянно):**
```cmd
setx QT_VERSION "6.10.0"
```
(Требует перезапуска терминала)

### 3. Сборка с Qt 6

```bash
cd substrata_build
cmake ../substrata --qtversion=6.10.0
cmake --build . --config Release
```

Или просто:
```bash
cmake ../substrata  # Автоматически использует QT_VERSION из окружения
```

### 4. Возврат к Qt 5

Просто уберите переменную окружения или установите:
```powershell
$env:QT_VERSION = "5.15.16"
```

## Работа с обновлениями из основного репозитория

### Стратегия

1. **Код универсален** - поддерживает обе версии Qt через `#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))`

2. **Конфликты будут минимальными:**
   - В основном в `config-lib.rb` (версия по умолчанию)
   - Возможно в `CMakeLists.txt` (пути к Qt)

3. **При получении обновлений:**
   ```bash
   git checkout master
   git fetch origin
   git merge origin/master
   # Решить конфликты в config-lib.rb если есть
   
   git checkout qt6-migration
   git merge master  # Подтянуть обновления
   ```

## Проверка версии Qt

```bash
cd substrata/scripts
ruby config.rb --qtversion
```

Должно показать текущую версию (5.15.16 по умолчанию или 6.10.0 если установлена переменная).

## Структура директорий Qt

- **Qt 5:** `C:\programming\Qt\5.15.16-vs2022-64\`
- **Qt 6:** `C:\programming\Qt\6.10.0\msvc2022_64\` или `C:\Qt\6.10.0\msvc2022_64\`

## Известные проблемы

1. **Qt Gamepad** - не доступен в Qt 6, код уже обрабатывает это
2. **QGLWidget → QOpenGLWidget** - автоматически переключается через условную компиляцию
3. **Core5Compat** - обязателен для Qt 6, обеспечивает совместимость с Qt 5 API

## Ветка qt6-migration

Все изменения для Qt 6 находятся в ветке `qt6-migration` для изоляции от основного кода.
