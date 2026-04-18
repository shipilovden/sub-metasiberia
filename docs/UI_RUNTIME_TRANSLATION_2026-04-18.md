# Runtime-перевод UI (2026-04-18)

Дата: 2026-04-18  
Область: `gui_client/MainWindow.*`, `gui_client/ObjectEditor.*`, `gui_client/RuntimeTranslation.*`

## Что было не так

- В UI уже было меню `Window -> Language -> English/Русский`, но переключатель фактически не применял перевод.
- Класс `RuntimeTranslator` существовал, но не был подключён к `QApplication`.
- В `MainWindow::changeEvent()` не обрабатывался `QEvent::LanguageChange`, из-за чего меню/действия не перерисовывались на новом языке.
- Для настроек аудио-плеера в `ObjectEditor` не было hover-подсказок.

## Что сделано

1. Подключён runtime-перевод в `MainWindow`:
- добавлен `RuntimeTranslator` в состояние `MainWindow`;
- добавлена логика `applyUILanguage(...)` с `installTranslator/removeTranslator`;
- добавлены обработчики:
  - `on_actionLanguage_English_triggered()`
  - `on_actionLanguage_Russian_triggered()`
- язык сохраняется в `QSettings` по ключу:
  - `setting/ui_language` (`en` / `ru`)

2. Добавлено обновление UI при смене языка:
- `MainWindow::changeEvent()` теперь обрабатывает `QEvent::LanguageChange`;
- вызывается `ui->retranslateUi(this)` и обновляются динамические элементы:
  - dock/action тексты в меню `Window`,
  - tooltip у кнопки открытия локации в браузере,
  - help-текст.

3. Включены tooltip-подсказки в меню:
- для пунктов меню-бара и вложенных подменю;
- для `QMenu` включён `setToolTipsVisible(true)`.

4. Обновлён `ObjectEditor`:
- добавлен `changeEvent()` с обработкой `QEvent::LanguageChange`;
- добавлен `retranslateDynamicAudioPlayerUI()` для динамически созданных контролов аудио-плеера;
- добавлены hover-подсказки для ключевых параметров аудио-плеера:
  - `Volume`, `Autoplay`, `Loop`, `Shuffle`,
  - `Activation Distance`, `Sound Radius`,
  - `3D Directionality`, `Directionality Focus`, `Directionality Sharpness`, `Directional Spread`,
  - `Use Daily Schedule`, `Schedule Start`, `Schedule End`,
  - `Playlist`.

5. Расширен словарь `RuntimeTranslation`:
- добавлены переводы для пунктов меню, динамических строк `MainWindow`;
- добавлены переводы для новых строк аудио-плеера и их tooltip-описаний.

6. Сборка:
- `gui_client` успешно собран в:
  - `RelWithDebInfo`
  - `Release`

## Примечание

- Файлы `RuntimeTranslation.cpp/h` добавлены в сборку через `gui_client/CMakeLists.txt`.
