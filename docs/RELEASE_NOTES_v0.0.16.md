# Metasiberia Beta v0.0.16

Дата релиза: 2026-03-25

## Что вошло в релиз

1. В Qt-клиент добавлена экспериментальная native OpenXR / VR-интеграция: сборка через `XR_SUPPORT=ON`, bootstrap OpenXR session, stereo frame lifecycle, mirror-preview в основном окне и безопасный fallback обратно в desktop-режим, если runtime или шлем недоступны.
2. В клиентских настройках и аргументах запуска появился явный выбор XR-режима: `auto`, `desktop`, `vr`, а также поддержка `--vr`, `--desktop` и legacy-переменной `SUBSTRATA_ENABLE_XR`.
3. В XR-слой добавлены базовые diagnostics и инфраструктура ввода: head tracking, per-eye view state, grip/aim pose для контроллеров, trigger/select diagnostics, ручной `recenter` и трассировка позы шлема в `xr_pose_trace.csv`.
4. Исправлена клиентская регрессия загрузки изображений относительно `0.0.10`: клиент снова использует стандартный путь выбора texture resources без хост-специфичного отключения `basis`/LOD, из-за которого новый клиент запрашивал у сервера другой набор ресурсов, чем старый рабочий билд.
5. Путь создания `image object` и загрузки материалов приведён к более стабильному поведению для Metasiberia worlds, чтобы новые и старые изображения проходили через единый клиентский pipeline выбора ресурсов.
6. Расширен editor workflow для text/web/audio-объектов: улучшено редактирование содержимого, подготовлена поддержка выбора шрифтов, доработано создание audio player panels и multi-file импорт аудио.
7. Webclient и web-поведение после `v0.0.15` дополнительно отполированы: улучшены loading overlay и chat toggle, убран проблемный путь с compressed webclient data responses, добавлена поддержка текстовых шрифтов и web emoji fonts.
8. Внутренние world/resource-подсистемы дополнительно подготовлены к более устойчивой работе с material/resource metadata, чтобы XR, web view и текстовые объекты использовали согласованную модель данных.

## Технические примечания

- Версия клиента: `0.0.16` (`shared/Version.h`).
- Версия Metasiberia: `0.0.16` (`shared/MetasiberiaVersion.h`).
- Целевой Windows-артефакт релиза: `MetasiberiaBeta-Setup-v0.0.16.exe`.
- Статус релиза: `Pre-release`.
