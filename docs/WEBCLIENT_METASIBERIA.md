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
