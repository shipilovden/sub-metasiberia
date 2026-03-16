# Chat Emoji Implementation 2026-03-17

## Что добавлено

- Emoji-кнопка в правом Qt chat dock.
- Emoji-кнопка в overlay-чате снизу слева.
- Единый whitelist emoji через `gui_client/EmojiUtils.h`.
- Локальный click-sound на отправку emoji через `resources/sounds/ms_chat_zvuk.mp3`.
- Отправка emoji как обычного `Protocol::ChatMessageID` без изменения сетевого протокола.
- Временный floating emoji над аватаром отправителя с плавным подъёмом и затуханием.

## Затронутые файлы

- `gui_client/MainWindow.ui`
- `gui_client/MainWindow.cpp`
- `gui_client/MainWindow.h`
- `gui_client/ChatUI.cpp`
- `gui_client/ChatUI.h`
- `gui_client/GUIClient.cpp`
- `gui_client/GUIClient.h`
- `gui_client/EmojiUtils.h`
- `gui_client/TestSuite.cpp`

## Архитектурная схема

1. Пользователь нажимает emoji-кнопку в Qt или overlay-чате.
2. Picker отправляет выбранный emoji через `GUIClient::sendEmojiChatMessage(...)` и локально проигрывает `ms_chat_zvuk.mp3`.
3. Сервер обрабатывает emoji как обычное чат-сообщение.
4. Клиент принимает входящий `Msg_ChatMessage`.
5. Если текст сообщения полностью совпадает с emoji из whitelist, клиент создаёт floating billboard над аватаром отправителя.

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
