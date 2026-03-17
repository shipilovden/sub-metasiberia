# Metasiberia Release Pipeline

## Цель

Для каждого тега релиза `vX.Y.Z` в GitHub Release должен быть Windows installer:

1. `MetasiberiaBeta-Setup-vX.Y.Z.exe`

## Как это работает

1. Локальный скрипт `scripts/publish_update.ps1`:
   - поднимает версию,
   - делает commit/tag/push,
   - собирает Windows installer,
   - публикует GitHub Release с Windows-артефактом.

2. Дополнительная Linux-догрузка в GitHub Release не используется.

## Как это делалось нормально раньше

Схема была и должна оставаться простой:

1. Поднять версию.
2. Собрать Qt5-клиент через `C:\programming\qt_build.ps1`.
   - Для точечной пересборки одной конфигурации допустимо использовать, например, `C:\programming\qt_build.ps1 -Configs RelWithDebInfo -SkipConfigure`.
3. Собрать `MetasiberiaBeta-Setup-vX.Y.Z.exe`.
4. Создать `GitHub Release` и прикрепить Windows installer.

Без отдельного Linux tarball, без release workflow-догрузки и без второго обязательного артефакта.
