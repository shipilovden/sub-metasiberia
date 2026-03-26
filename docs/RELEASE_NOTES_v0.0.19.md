# Metasiberia Beta v0.0.19

Дата релиза: 2026-03-26

## Что вошло в релиз

1. В Windows Qt-клиент добавлен экспериментальный `OpenXR` / `VR` режим: Метасибирь теперь умеет запускаться в шлеме с настоящим стерео-рендером, трекингом головы и трекингом контроллеров, а не только в обычном desktop-окне.
2. Добавлен и стабилизирован XR recenter-пайплайн: ручное выравнивание теперь можно вызвать клавишами `Home` или `End`, а клиент пишет `%APPDATA%/Cyberspace/xr_pose_trace.csv` для точной диагностики высоты головы, горизонта, симметрии глаз и корректности OpenXR-калибровки.
3. Добавлен `analyze_xr_pose_trace.py` и обновлена внутренняя документация по XR trace-анализу, чтобы быстро различать проблемы floor space, наклона шлема, recenter и grounding аватара.
4. Исправлены VR-ошибки grounding и first-person отображения аватара: посадка модели теперь привязывается к `LeftEye` / `RightEye` / `Head`, локальная голова в XR first-person больше не лезет в камеру, а нейтральная высота игрока стала значительно стабильнее.
5. В OpenXR action backend добавлены оба режима передвижения: `teleport locomotion` по лучу с валидной/невалидной точкой посадки и `smooth locomotion` на стиках/трекпадах для движения и поворота.
6. Расширена поддержка controller interaction profiles: кроме уже существующих профилей теперь явно покрываются `Meta Quest 2`, `Meta Touch Plus`, `Meta Touch Pro`, `HTC Vive`, `HTC Vive Cosmos`, `HTC Vive Focus 3`, `Valve Index`, `Microsoft motion controllers` и `Khronos generic controller`.
7. Для VIVE Focus 3 клиент теперь умеет загружать app-side render models контроллеров из локального `VIVE Business Streaming`, благодаря чему контроллеры остаются видимыми после hand-off фокуса и не зависят от временного compositor overlay.
8. Исправлен XR visual path: восстановлены нормальный portal rendering и финальный imaging-проход в шлеме, так что порталы больше не превращаются в белые полотна, а VR-картинка снова использует корректный tonemapping/bloom вместо заметно более тёмного изображения.
9. Добавлены утилиты для workflow вокруг VR-отладки: автоматическая синхронизация новых записей со шлема через `adb` в локальную папку `C:\programming\recordings`, включая watch-режим и bat-стартер для двойного клика.

## Технические примечания

- Версия клиента: `0.0.19` (`shared/Version.h`).
- Версия Metasiberia: `0.0.19` (`shared/MetasiberiaVersion.h`).
- Целевой Windows-артефакт релиза: `MetasiberiaBeta-Setup-v0.0.19.exe`.
- Статус релиза: `Pre-release`.
- Текущая основная отладка и проверка проведены на `HTC VIVE Focus 3`.
- Поддержка `Quest 2 / Quest 3` в этой итерации относится к `PCVR/OpenXR` сценарию, а не к отдельному standalone Android APK.
