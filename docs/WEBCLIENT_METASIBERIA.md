# Metasiberia WebClient: Build, Deploy, Android Readiness

## 1. Что где лежит

- HTML-шаблон webclient: `webclient/webclient.html`
- Скрипт подготовки preload-данных для Emscripten: `scripts/make_emscripten_preload_data.rb`
- Скрипт cache-busting hash-замен: `scripts/update_webclient_cache_busting_hashes.rb`
- Публичные web-файлы сайта: `webserver_public_files/`
- HTML-фрагменты сайта: `webserver_fragments/`
- Иконки HUD клиента (источник): `resources/buttons/`

## 2. Как сейчас синхронизируются иконки webclient

- В `webclient/webclient.html` добавлен preRun-override, который подгружает иконки из:
  - `/webclient/data/resources/buttons/<icon>.png?hash=WEBCLIENT_BUTTONS_HASH`
- Иконки монтируются в виртуальную FS Emscripten по пути:
  - `/data/resources/buttons/...`
- Это гарантирует, что webclient использует те же HUD-иконки, что и desktop fullscreen-клиент.

## 3. Build-пайплайн для webclient

1. Сгенерировать preload-данные:
   - `ruby scripts/make_emscripten_preload_data.rb C:\programming\substrata`
2. Собрать Emscripten webclient (стандартный шаг вашей сборки `gui_client` для web).
3. Обновить cache-busting хэши:
   - `ruby scripts/update_webclient_cache_busting_hashes.rb C:\programming\substrata`

Что делает `make_emscripten_preload_data.rb` дополнительно:
- Копирует `resources/buttons/*` в:
  - `$CYBERSPACE_OUTPUT/data/resources/buttons`
  - `$CYBERSPACE_OUTPUT/test_builds/data/resources/buttons`

## 4. Деплой на Metasiberia v2

Используется:
- `scripts/deploy_web_to_metasiberia_v2.ps1`

Теперь скрипт умеет:
- деплоить `webserver_public_files`
- деплоить `webserver_fragments`
- деплоить `webclient/webclient.html`
- деплоить `resources/buttons/*` в серверный `webclient/data/resources/buttons`

Опции пропуска:
- `-SkipPublicFiles`
- `-SkipFragments`
- `-SkipWebClientHtml`
- `-SkipWebClientButtons`

## 5. Android из webclient: что реально

Да, Android-приложение можно делать из webclient, есть 2 рабочих пути:

1. WebView wrapper (быстрее запуск)
- Android app с `WebView`, который открывает `https://vr.metasiberia.com/webclient`
- Плюсы: быстрый старт, полный контроль в native shell
- Минусы: чуть больше поддержки Java/Kotlin-кода

2. TWA (Trusted Web Activity, через Chrome)
- По сути PWA как Android app
- Плюсы: меньше native-кода
- Минусы: требуется корректная PWA-конфигурация и Digital Asset Links

## 6. Минимальный чеклист readiness для Android

- Стабильный URL webclient: `https://vr.metasiberia.com/webclient`
- Корректные touch-контролы и mobile layout
- HTTPS без mixed content
- Политика разрешений (микрофон/камера) для мобильного браузера/WebView
- Иконки и branding синхронизированы с desktop HUD

## 7. Что важно помнить в проде Linux

- На Linux inotify watcher не всегда ловит изменения в глубине поддиректорий.
- После деплоя webclient-поддиректорий нужно сделать "ping" в корне webclient dir (скрипт деплоя это делает), чтобы сервер перезагрузил web-данные.

## 8. 2026-02-20 Emergency Fix (Gesture Manager and Webcam Icon)

Problem seen in production:
- Webclient loaded, but no `+` button, no gesture manager panel, and no webcam button.
- Root cause was old `gui_client.wasm` on server, not only stale browser cache.

How to prove wasm is correct:
- Check strings inside wasm:
  - `Manage gestures`
  - `Close gesture manager`
  - `Enable webcam`
  - `Add gesture button`

If these strings are missing, rebuild and redeploy Emscripten artifacts.

Build + deploy flow used:
1. Build webclient wasm/js/data in Emscripten build tree.
2. Run hash replacement:
   - `$env:CYBERSPACE_OUTPUT='C:\programming\substrata_output_emscripten'`
   - `ruby scripts/update_webclient_cache_busting_hashes.rb C:\programming\substrata`
3. Deploy to Metasiberia v2:
   - copy `gui_client.js`, `gui_client.wasm`, `gui_client.data`, `webclient.html` to:
     - `/root/cyberspace_server_state/webclient/`
4. Verify hashes on server with `sha256sum`.
5. Verify live HTML has new hashes via:
   - `curl https://vr.metasiberia.com/webclient`

Important source fixes for Emscripten build:
- `gui_client/PhotoModeUI.cpp`
  - direct Telegram upload thread is compiled only for non-Emscripten;
  - webclient keeps server-side photo upload path.
- `gui_client/SDLUIInterface.cpp`
  - AES include guarded out for Emscripten;
  - password fallback supports plain-text `credentials/.../password`.
- `gui_client/CMakeLists.txt`
  - `VRoidAuthFlow.cpp` is now only in non-SDL/Qt build path.
- `scripts/make_emscripten_preload_data.rb`
  - Ruby 3.4 compatibility fix: `File.exists?` -> `File.exist?`.

Browser-side note:
- After deploy, use `Ctrl+F5`.
- If still stale, clear site data for `vr.metasiberia.com` and reload.

## 9. 2026-02-20 Gesture Manager Icon

- `Manage gestures` button icon in webclient is set to `plus.png` (same visual icon as add gesture action).
- Source: `gui_client/GestureUI.cpp` (`edit_gestures_button`).

## 10. 2026-03-17 Floating Chat Rollout

- Webclient uses the same shared `GUIClient` path as the desktop client, so floating emoji and floating text previews above avatars ship to web through the normal Emscripten `gui_client` rebuild.
- During the rebuild, two web-only blockers were fixed:
  - `glare-core/networking/EmscriptenWebSocket.h`: added a no-op `setTimeout()` override to stay compatible with the shared `SocketInterface` contract.
  - `gui_client/PhotoModeUI.cpp`: VK HTTP helper code is now compiled only for non-Emscripten targets, matching the existing desktop-only upload flow.
- Verified build output in `C:\programming\substrata_output\test_builds`:
  - `gui_client.js`
  - `gui_client.wasm`
  - `gui_client.data`
  - `webclient.html`
- Deployed these four files to `/root/cyberspace_server_state/webclient/` on `Metasiberia v2` and confirmed live `/webclient` serves the updated cache-busting hashes.
