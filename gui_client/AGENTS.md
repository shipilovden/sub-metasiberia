# Локальные инструкции: `gui_client`

Дополняет [корневой AGENTS.md](../AGENTS.md). Архитектура client: [docs/codex/architecture.md](../docs/codex/architecture.md#native-client).

## Граница

- Target `gui_client`; Qt entry `MainWindow.cpp::main`, SDL/Emscripten entry `SDLClient.cpp::main`.
- `GUIClient` владеет общей world/network/resource/render логикой; Qt/SDL — UI boundaries.
- Emscripten использует common/SDL path. В `GUIClient` и его публичных headers запрещены безусловные Qt include/type/JSON/image/widget dependencies: этот код компилируется и для `USE_SDL=ON`.
- `#if !defined(USE_SDL)` должен закрывать **все** точки Qt-зависимости: include, declaration/signature, member/inline use, definition и call site. Guard только вокруг тела функции не защищает SDL/Web от Qt leakage.
- Desktop-only editor/adapter выносить на Qt boundary; не добавлять Qt convenience code в common path ради одной платформы.

## Перед изменением

- Проверить `git diff -- gui_client` и сохранить active Particle/Scientific WIP.
- Искать конкретный widget/manager/symbol; большие `MainWindow.cpp`/`GUIClient.cpp` читать диапазонами.
- Для UI сопоставить `.ui/.h/.cpp`, action/owner, translations и CMake/MOC.

## Инварианты

- `USE_SDL=OFF` — Qt; `ON` — SDL/Web path.
- Qt build не является заменой SDL/Emscripten build. При правке shared `GUIClient` или его headers Web scope считается неподтверждённым до успешной изолированной Emscripten-сборки `gui_client`.
- XR optional/native-only; desktop работает без runtime/SDK. Не лечить avatar visibility camera offset и не добавлять лишний full mirror render.
- Map world Mercator scale не зависит от avatar altitude/MiniMap zoom.
- Resource/Basis fallbacks учитывать desktop/web/XR и не возвращать white textures.
- Chat/private/attachments меняют protocol/server вместе с UI; recipient UID первичен.
- Новый editor сохраняет selection, undo/dirty flags, permissions, transform lifecycle и authoritative server validation.

## Scientific Object WIP

- Канон: [scientific-object-editor.md](../docs/codex/scientific-object-editor.md).
- Current envelope: generic `WorldObject` + marker `metasiberia_scientific_object_v1` + JSON in `content`.
- Общий limit `WorldObject::MAX_CONTENT_SIZE` — 10 000 bytes; контролировать итоговый payload.
- UI database/provider/file lists и mock/template output не являются working adapters.
- Scientific temp OBJ остаётся промежуточным: сохранять existing `GUIClient::objectEdited()` conversion в `.bmesh`, checksum URL и resource upload path; проверять reload отдельно.
- API key не сериализовать и не логировать; current QSettings storage требует review.
- Qt WIP не считать автоматически поддержанным SDL/Web/XR.

## Проверка

Выбирать минимальную строку матрицы в [build-and-test.md](../docs/codex/build-and-test.md). UI требует compile + manual flow; shared/protocol change добавляет server. Для общей правки, затрагивающей SDL/Web, обязательна изолированная Emscripten-сборка `gui_client`; без неё нельзя deploy/publish Web bundle. В docs-only задаче build/runtime не запускать.
