# Qt Themes Menu (2026-04-17)

В Qt-клиенте Metasiberia добавлено меню `Themes` в верхней строке меню (`MainWindow`) между `Window` и `Help`.

## Что делает

- Показывает выпадающий список доступных тем.
- Применяет выбранную тему ко всему `QApplication` через `QPalette`.
- Сохраняет выбор в `QSettings` по ключу:
  - `setting/qt_theme_name`
- Опция `Default` сбрасывает тему на стандартную Qt-палитру.

## Источник тем

Используется набор тем из репозитория:

- https://github.com/beatreichenbach/qt-themes

Темы в проекте лежат в:

- `resources/qt_themes/*.json`

Во время копирования runtime-файлов они попадают в output:

- `data/resources/qt_themes/*.json`

## Включённые темы

- `atom_one`
- `blender`
- `catppuccin_frappe`
- `catppuccin_latte`
- `catppuccin_macchiato`
- `catppuccin_mocha`
- `dracula`
- `github_dark`
- `github_light`
- `modern_dark`
- `modern_light`
- `monokai`
- `nord`
- `one_dark_two`

Лицензия upstream добавлена как:

- `resources/qt_themes/LICENSE.qt-themes`
