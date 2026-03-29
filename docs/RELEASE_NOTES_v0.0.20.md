# Metasiberia Beta v0.0.20

Дата релиза: 2026-03-29

## Что вошло в релиз

1. Уточнено XR first-person поведение локального аватара в Qt/OpenXR клиенте: при активной VR-сессии локальная модель снова скрывается, чтобы камера в шлеме не смотрела через собственную голову и лицо.
2. Исправлен companion-render path для активной XR-сессии: desktop-окно больше не должно запускать лишний полный scene render только из-за кратковременного флапа mirror-view, что снижает риск увидеть compositor / SteamVR background на резких поворотах головы.
3. Исправлена логика вертикального XR anchor: высота камеры больше не зависит напрямую от случайной высоты шлема в момент `FOCUSED` / recenter-калибровки, а привязывается к стабильному engine eye-height baseline.
4. Зафиксирован текущий проверенный baseline высоты XR-камеры: `XR_WORLD_VERTICAL_TRIM_METRES = 0.9f` в `gui_client/XRSession.cpp`.
5. Обновлена release-инструкция: git commit/tag/push остаются частью release-процесса, но Windows installer `.exe` загружается вручную владельцем релиза после локальной проверки сборки.

## Технические примечания

- Версия клиента: `0.0.20` (`shared/Version.h`).
- Версия Metasiberia: `0.0.20` (`shared/MetasiberiaVersion.h`).
- Целевой Windows-артефакт релиза: `MetasiberiaBeta-Setup-v0.0.20.exe`.
- Статус релиза: `Pre-release`.
- Windows installer `.exe` не загружается automation-скриптом в рамках git/tag шага; он догружается вручную владельцем релиза.
