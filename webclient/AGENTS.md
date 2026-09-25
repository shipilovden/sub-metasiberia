# Локальные инструкции: `webclient`

Дополняет [корневой AGENTS.md](../AGENTS.md).

## Граница

- `webclient.html` — browser shell для Emscripten build target `gui_client`.
- Gameplay/UI/network C++ находится в `../gui_client`; generated JS/WASM/data не source.
- Embedded server отдаёт output через `/webclient`/`WebDataStore`; realtime идёт по WebSocket общим protocol.

## Уникальные правила

- Не редактировать generated JS/WASM/data.
- Resource URL учитывает preload FS, cache hashes и server path.
- Preload/hash scripts имеют side effects; запускать только по подтверждённому isolated workflow.
- Qt-only Scientific Object UI не считать Web Client feature без отдельной implementation/parity decision.
- Historical v2 deploy paths/scripts не использовать как current production flow.
- Web bundle не является побочным продуктом Qt build. Никогда не заменять failed/missing Emscripten сборку старым JS/WASM/data или desktop output.
- При изменении общего `gui_client` сначала проверить отсутствие Qt-only includes/types в SDL path. Compile guard должен охватывать include, declaration, definition и call site, а не только тело функции.
- При ошибке Emscripten compilation остановиться до preload/hash/deploy: исправить source boundary и собрать новый isolated bundle. Недоступный toolchain — blocker, а не основание публиковать непроверенный Web комплект.

Emscripten compile + local HTTP/WSS smoke требуются только когда Web scope реально затронут; иначе явно отметить непроверенное. См. [build-and-test.md](../docs/codex/build-and-test.md).
