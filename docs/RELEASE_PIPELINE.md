# Metasiberia Release Pipeline

## Цель

Для каждого тега релиза `vX.Y.Z` в GitHub Release должны быть два артефакта:

1. `MetasiberiaBeta-Setup-vX.Y.Z.exe` (Windows installer)
2. `MetasiberiaBeta-Linux-vX.Y.Z.tar.gz` (Linux package)

## Как это работает

1. Локальный скрипт `scripts/publish_update.ps1`:
   - поднимает версию,
   - делает commit/tag/push,
   - собирает Windows installer,
   - публикует GitHub Release с Windows-артефактом,
   - запускает Linux workflow `release-linux-asset.yml`.

2. GitHub Actions workflow `.github/workflows/release-linux-asset.yml`:
   - триггерится на `release: published/prereleased` (и вручную через `workflow_dispatch`),
   - собирает Linux `gui_client` (SDL, `CEF_SUPPORT=OFF`),
   - упаковывает tarball,
   - загружает его в тот же GitHub Release.

## Ручной запуск Linux-артефакта

Если нужно догрузить Linux пакет для уже существующего релиза:

```bash
gh workflow run release-linux-asset.yml -R shipilovden/sub-metasiberia -f tag=vX.Y.Z
```

После успешного выполнения workflow второй артефакт появится в `Releases` рядом с Windows installer.
