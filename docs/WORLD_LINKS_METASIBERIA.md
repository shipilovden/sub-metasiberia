# Metasiberia: ссылки для открытия миров в браузере

Базовый домен:

- `https://vr.metasiberia.com`

## 1. Личный мир пользователя

Обычно личный мир имеет имя, равное имени пользователя.

- Страница мира:
  - `https://vr.metasiberia.com/world/<world_name>`
- Сразу открыть WebClient в этом мире:
  - `https://vr.metasiberia.com/visit?world=<world_name>`

Пример:

- `https://vr.metasiberia.com/world/Denis+Shipilov`
- `https://vr.metasiberia.com/visit?world=Denis+Shipilov`
- `https://vr.metasiberia.com/visit?world=LANADEARTA`

## 2. Дополнительный мир пользователя (формат `username/world`)

Если мир создан как дочерний, имя может быть вида `username/world`.

- Страница мира:
  - `https://vr.metasiberia.com/world/<username>/<world>`
- Сразу открыть WebClient:
  - `https://vr.metasiberia.com/visit?world=<username>%2F<world>`

Пример:

- `https://vr.metasiberia.com/world/denis/My+Build+Zone`
- `https://vr.metasiberia.com/visit?world=denis%2FMy+Build+Zone`

## 3. Основной мир (root world)

- Страница основного мира:
  - `https://vr.metasiberia.com/world/`
- Сразу открыть WebClient в основном мире:
  - `https://vr.metasiberia.com/visit`

## 4. Ссылка с координатами (опционально)

Можно добавить позицию и направление:

- `https://vr.metasiberia.com/visit?world=<world_name>&x=<X>&y=<Y>&z=<Z>&heading=<deg>`

Для основного мира параметр `world` можно не передавать:

- `https://vr.metasiberia.com/visit?x=<X>&y=<Y>&z=<Z>&heading=<deg>`

## 5. Важные примечания

- Для основного мира используйте именно `/world/` (со слэшем в конце).
- Имена мира в URL должны быть URL-encoded (пробелы: `+` или `%20`).
- Для WebClient используйте путь `/visit` (это корректная публичная точка входа).
