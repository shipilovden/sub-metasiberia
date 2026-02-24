# Metasiberia Admin UI Backlog (Planned)

Дата: 2026-02-24

## Цель
Сделать верхнюю админ-навигацию и рабочие сценарии супер-админа более быстрыми, интерактивными и удобными для ежедневных операций.

## Planned Next Improvements

1. Global Search
- Единый поиск по `user id/username`, `parcel id`, `world`, `chatbot id/name`.
- Быстрый переход к нужной сущности из одного поля.

2. Quick Actions
- Блок быстрых действий в 1 клик:
- `Create parcel`, `Create world parcel`, `Create chatbot`, `Reload web data`, `Regen map`.

3. Alerts with Counters
- Индикаторы проблем в шапке:
- `notdone screenshots`, `notdone map tiles`, `disabled critical flags`, `error states`.

4. Recent Entities
- Блок последних открытых сущностей:
- users, parcels, worlds, chatbots.

5. Saved Filters
- Сохранение/восстановление фильтров для:
- `/admin_parcels`, `/admin_world_parcels`, `/admin_chatbots`.

6. Keyboard Shortcuts
- Горячие клавиши для:
- фокуса на поиске, переходов по основным страницам, открытия последних сущностей.

7. Ops Panel
- Быстрый операционный статус:
- web assets hash, feature flags, map queue, screenshot bot health.

8. Audit Quick View
- Мини-лента последних admin-действий прямо на `/admin` с фильтрацией.

## Notes
- Визуальный инвариант верхней навигации сохраняется:
- одна строка компактных кнопок, без переноса на второй ряд на desktop.
- Для узких экранов допускается горизонтальный скролл.
