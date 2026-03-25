# Metasiberia Beta v0.0.18

Дата релиза: 2026-03-25

## Что вошло в релиз

1. Исправлен путь поиска пользовательских шрифтов в установленном Windows-клиенте: `ObjectEditor` теперь в первую очередь ищет шрифты в `data/resources/fonts`, то есть там, куда их реально кладёт packaged build и installer.
2. Синхронизирован runtime-resolver для 3D-текста: клиент теперь одинаково ищет world-text fonts и в packaged layout, и в legacy/dev layout, без зависимости от локального пути разработчика `C:/programming/substrata/resources/fonts`.
3. Устранена причина, из-за которой одна и та же версия клиента могла показывать разные наборы шрифтов на разных ПК: машина с исходниками случайно подхватывала dev fonts, а чистая установленная машина оставалась только с `Default`.
4. Обновлена внутренняя документация по font pipeline, чтобы зафиксировать packaged runtime path и исключить повтор этой регрессии при следующих релизах.

## Технические примечания

- Версия клиента: `0.0.18` (`shared/Version.h`).
- Версия Metasiberia: `0.0.18` (`shared/MetasiberiaVersion.h`).
- Целевой Windows-артефакт релиза: `MetasiberiaBeta-Setup-v0.0.18.exe`.
- Статус релиза: `Pre-release`.
