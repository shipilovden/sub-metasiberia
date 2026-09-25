# Локальные инструкции: `substrata`

Дополняет [общие правила](../AGENTS.md). Эти правила обязательны для native Qt, SDL/Emscripten и Web-client путей.

## Граница платформ

- Qt desktop и SDL/Emscripten — разные продукты. Успешная Qt-сборка никогда не подтверждает Web-клиент и не разрешает подменять его артефакты.
- Код, который компилируется при `USE_SDL=ON` (включая общий `GUIClient`, `shared` и их публичные headers), не должен безусловно включать или упоминать Qt-типы, Qt JSON/изображения, QWidget-редакторы и другие desktop-only зависимости.
- `#if !defined(USE_SDL)` обязан охватывать include, declaration, signature, member/inline use, definition и call site. Guard только вокруг тела функции недостаточен, если header уже требует Qt.
- Предпочтительный путь для desktop-only функции — отдельный Qt adapter/boundary. Не расширять общий код Qt-зависимостями ради удобства одной платформы.
- Нельзя разворачивать stale Web bundle как обход неуспешной или не запущенной Emscripten-сборки. Сначала исправить platform boundary, затем собрать изолированный Emscripten target и проверить его артефакты.

## Обязательная проверка при Web-затрагивающей правке

1. Проверить включения/типы/символы на Qt leakage в common SDL path.
2. Собрать `gui_client` в изолированном Emscripten tree по [docs/codex/build-and-test.md](docs/codex/build-and-test.md#emscriptenwebclient).
3. Только после успешной сборки запускать preload/cache-hash scripts и рассматривать deploy нового Web bundle.

Если Emscripten toolchain недоступен или сборка не проходит, сообщить это как blocker. Не называть Web-поддержку готовой и не публиковать неподтверждённый комплект.
