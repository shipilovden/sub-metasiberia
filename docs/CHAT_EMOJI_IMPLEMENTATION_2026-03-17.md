# Chat Emoji Implementation 2026-03-17

## Что добавлено

- Emoji-кнопка в правом Qt chat dock.
- Emoji-кнопка в overlay-чате снизу слева.
- Единый whitelist emoji через `gui_client/EmojiUtils.h`.
- Локальный click-sound на отправку emoji через `resources/sounds/ms_chat_zvuk.mp3`.
- Отправка emoji как обычного `Protocol::ChatMessageID` без изменения сетевого протокола.
- Временный floating emoji над аватаром отправителя с плавным подъёмом и затуханием.
- Обычные чат-фразы теперь тоже появляются над головой аватара и улетают вверх тем же способом, но в компактной текстовой карточке.

## Затронутые файлы

- `gui_client/MainWindow.ui`
- `gui_client/MainWindow.cpp`
- `gui_client/MainWindow.h`
- `gui_client/ChatUI.cpp`
- `gui_client/ChatUI.h`
- `gui_client/GUIClient.cpp`
- `gui_client/GUIClient.h`
- `gui_client/EmojiUtils.h`
- `gui_client/FloatingChatMessageUtils.h`
- `gui_client/TestSuite.cpp`

## Архитектурная схема

1. Пользователь нажимает emoji-кнопку в Qt или overlay-чате.
2. Picker отправляет выбранный emoji через `GUIClient::sendEmojiChatMessage(...)` и локально проигрывает `ms_chat_zvuk.mp3`.
3. Сервер обрабатывает emoji как обычное чат-сообщение.
4. Клиент принимает входящий `Msg_ChatMessage`.
5. Клиент создаёт floating billboard над аватаром отправителя:
   - для whitelist emoji это прозрачный emoji billboard,
   - для обычного текста это короткая preview-фраза в компактной карточке.

## Источник emoji

- Windows-клиент использует системный emoji font `Segoe UI Emoji` (`Seguiemj.ttf`), который уже загружается в клиенте.
- Для 3D floating emoji используется прозрачная RGBA-текстура, построенная через существующий `TextRenderer`.

## Проверка

- Локальная сборка:
  - `cmake --build C:\programming\substrata_build_qt --config Release --target gui_client -- /m:4`
- Результат:
  - `C:\programming\substrata_output\vs2022\cyberspace_x64\Release\gui_client.exe`

## 2026-03-17 update

- Expanded the shared emoji whitelist to 130 entries.
- Added Russian category names: `Лица`, `Эмоции`, `Жесты`, `Эффекты`, `Животные`, `Сердца`.
- Extended the set with many face/reaction emoji based on the provided screenshot, plus dedicated `Животные` and `Сердца` categories.
- Qt chat picker and overlay chat picker both read the same category data from `gui_client/EmojiUtils.h`.
- Qt chat uses a larger popup window with tabs instead of a one-column menu.
- Overlay chat uses category switching inside the GL picker window, and the picker body size is computed from the active category so the available emoji count matches Qt.

## 2026-03-17 layout update

- Expanded the shared supported emoji set to 175 entries.
- Added a shared recent-emoji history that is stored in client settings and reused by both chat UIs.
- Qt chat now rebuilds its popup from the shared picker data, uses larger emoji buttons, and shows a recent-history tab plus scrollable category pages.
- Overlay chat now renders category buttons in a single top row, uses the same recent-history source, and widens the picker so the right edge is no longer clipped.

## 2026-03-17 hover and layout polish

- Restored the shared emoji catalog and expanded it to 227 entries for both chat UIs.
- The overlay emoji picker is now pushed to the right of the message panel so it does not cover the visible chat lines.
- Both chat pickers now show an emoji name tooltip when the cursor hovers a specific emoji.
- The Qt emoji popup uses larger emoji glyphs and a larger popup size so the emoji fills each tile more cleanly.
- Selecting an emoji no longer closes the picker in either chat UI; it now stays open for repeated sends and closes only by the close button or an outside click.
- The overlay emoji picker now uses a fixed-size window with wheel scrolling instead of growing taller to fit the full emoji list.
- The overlay category row is fixed at the top of the picker, while only the emoji grid underneath scrolls.
- The overlay category buttons now render above the window background and use a stronger active state so they remain clickable and visually distinct.
- The overlay category row now uses each button's real rendered width for equal spacing, and the emoji grid is anchored closer to the category strip.
- The shared emoji catalog now pulls in a near-complete Unicode picker set from the local `emoji-test.txt` reference, keeping the existing 6 groups and excluding only skin-tone variants and tag-sequence variants.

## 2026-03-17 render compatibility fix

- Replaced the auto-expanded emoji catalog with a curated render-safe subset shared by both chat UIs.
- Removed complex ZWJ sequences, flags, and other multi-codepoint combinations that the current GL text renderer does not shape reliably.
- `Жесты`, `Эффекты`, `Животные`, and `Сердца` now use only visually stable entries, so the picker grid stays aligned and no longer shows empty square placeholders.

## 2026-03-17 floating chat phrases

- The floating-overhead chat effect is no longer limited to emoji-only messages.
- Incoming regular chat text now spawns the same upward-moving overhead effect above the sender avatar.
- Floating text previews are sanitised, whitespace-normalised, and truncated to a short single-line preview so they stay readable and do not produce oversized world billboards.
