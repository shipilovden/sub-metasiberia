# Metasiberia Beta v0.0.15

Дата релиза: 2026-03-18

## Что вошло в релиз

1. Полностью переработан emoji picker в desktop-клиенте: категории выровнены, сетка стала ровной и предсказуемой, добавлена общая история недавних emoji и убраны пустые квадратные плейсхолдеры в проблемных группах.
2. Общий каталог emoji переведён на curated render-safe набор, поэтому группы `Жесты`, `Эффекты`, `Животные` и `Сердца` теперь используют только визуально стабильные символы, которые корректно рендерятся текущим текстовым и GL UI пайплайном.
3. Emoji picker унифицирован между Qt chat dock и нижним overlay-чатом: обе версии читают один и тот же каталог, одинаково показывают категории, recent history и tooltip с именем emoji.
4. Floating-эффект над головой аватара расширен с emoji на обычные чат-сообщения: теперь короткая preview-фраза тоже появляется над отправителем и плавно улетает вверх тем же способом, что и emoji.
5. Floating chat previews нормализуют пробелы, сводятся к одной строке и подрезаются до компактной длины, чтобы над аватаром не появлялись слишком большие world-billboard карточки.
6. Webclient получил тот же функционал floating emoji и floating chat messages, что и desktop-клиент, потому что использует общий `GUIClient` путь в Emscripten-сборке.
7. Для webclient устранены два web-only блокера сборки: добавлен совместимый no-op `setTimeout()` в `EmscriptenWebSocket`, а desktop-only VK/photo helper код в `PhotoModeUI` исключён из Emscripten-конфигурации.
8. На `Metasiberia v2` обновлены webclient-артефакты `gui_client.js`, `gui_client.wasm`, `gui_client.data` и `webclient.html`, так что браузерная версия уже обслуживает новую overhead-анимацию и актуальные cache-busting hash-ссылки.

## Технические примечания

- Версия клиента: `0.0.15` (`shared/Version.h`).
- Версия Metasiberia: `0.0.15` (`shared/MetasiberiaVersion.h`).
- Целевой Windows-артефакт релиза: `MetasiberiaBeta-Setup-v0.0.15.exe`.
- Linux-сборка в этот релиз не входит.
- Статус релиза: `Pre-release`.
