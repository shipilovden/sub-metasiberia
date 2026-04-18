# UI Translation Hotfix (2026-04-18)

Область: `gui_client/MainWindow.cpp`, `gui_client/ObjectEditor.cpp`

## Что исправлено

1. Устойчивое переключение языка
- Язык читается из `setting/ui_language` с fallback на legacy `ui/language`.
- При переключении язык пишется в оба ключа: `setting/ui_language` и `ui/language`.
- Текущий язык также публикуется в `QApplication` property: `metasiberia.ui_language`.

2. Исправлена кодировка русских переводов (mojibake `����`)
- Причина: `RuntimeTranslation.cpp` содержал русские string literals, а в коде они декодируются через `QString::fromUtf8(...)`.
- На MSVC без явного `/utf-8` narrow literals попадают в ACP (`CP1251`), из-за чего `fromUtf8` давал символы замены `�`.
- Фикс в сборке: для `RuntimeTranslation.cpp` добавлен `COMPILE_OPTIONS "/utf-8"` в `gui_client/CMakeLists.txt`.

3. Крэш на старте в режиме `Русский` устранён
- Причина: нестабильный массовый `QAction::setText(...)` при runtime-обходе всех actions в `MainWindow::refreshTranslatedUiText()`.
- Симптом: `APPCRASH 0xc0000005` при запуске (то состояние, что было на скриншоте).
- Фикс: убран mass-update текста action'ов; перевод menu/action остаётся через штатную Qt-цепочку `QTranslator + retranslateUi`.

4. Tooltip-подсказки в меню
- В `MainWindow::updateMenuTooltips()` оставлены tooltip/status tip для пунктов меню и вложенных подменю.

5. Перевод секции Audio Player в Object Editor
- `ObjectEditor::retranslateDynamicAudioPlayerUI()` переведён на прямой runtime-перевод.
- Переводятся заголовки, подписи, кнопки, суффиксы (`m`, `deg`, `h`) и tooltip'ы.
- Заголовок группы `Audio` / `Audio Player` обновляется корректно при смене языка.

## Сборка

Проверено локальной сборкой:

```powershell
cmake --build C:\programming\substrata_build_qt --config RelWithDebInfo --target gui_client
```

Выходной бинарник:

- `C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe`

## Дополнение (Avatar Settings)

Обновлено окно настроек аватара (`AvatarSettingsWidget` / legacy `AvatarSettingsDialog`):

- добавлены runtime-переводы для вкладок и полей:
  - `Model`
  - `Preview animation`
  - `Avatar` / `VRoid`
  - `Login` / `Fetch models` / `Logout`
  - текст workflow и ссылка `Open VRoid Hub`
- добавлен перевод динамических строк (ReadyPlayerMe / AvaturnMe блок) в `AvatarSettingsWidget.cpp`;
- добавлен `changeEvent(QEvent::LanguageChange)` в `AvatarSettingsWidget` для корректной смены языка без перезапуска;
- добавлены переводы статусных строк `VRoidAuthFlow`, чтобы статус в блоке VRoid отображался по-русски;
- добавлены переводы стандартных кнопок `QPlatformTheme`:
  - `Apply` -> `Применить`
  - `Close` -> `Закрыть`

## Дополнение (MainWindow title)

- после `LanguageChange` заголовок главного окна сбрасывался в `MainWindow` из `ui_MainWindow.h` (`retranslateUi()`).
- исправлено в `MainWindow::refreshTranslatedUiText()`:
  - сразу после `ui->retranslateUi(this)` принудительно восстанавливается продуктовый заголовок из `computeWindowTitle()`:
    - `Metasiberia Beta v<version>`
