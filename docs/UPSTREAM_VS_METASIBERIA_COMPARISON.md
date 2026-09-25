# Сравнение upstream `glaretechnologies/substrata` и форка `shipilovden/sub-metasiberia`

## 1. Краткий вывод

Это не просто косметический fork и не просто “переименованный upstream”. По данным git-анализа и содержимому репозитория это серьёзное downstream-разветвление, в котором:

- Substrata используется как базовая платформа/движок;
- поверх неё добавлен продуктовый слой Metasiberia;
- появилась отдельная веб/админ инфраструктура, кастомные страницы и production-сценарии;
- добавлены уникальные игровые функции (боты/AI, avatar gear, map portal tooling, UI/branding, XR fixes);
- изменён маркетинговый и deployment контекст под Metasiberia.

Итог: fork уже превратился из “чистой upstream-ветки” в самостоятельный проект на базе Substrata.

## 2. Методика проверки

Проверка выполнялась по нескольким независимым источникам:

1. `git status`, `git branch -vv`, `git remote -v` в локальном репозитории;
2. `git fetch --all --prune` и сравнение веток `upstream/master` и `origin/master`;
3. анализ `git diff --stat`, `git diff --name-status` и `git log --left-right`;
4. сравнение ключевых файлов и README между upstream и форком;
5. проверка кастомной документации и feature-notes в `docs/`.

## 3. Важная статистика по divergence

По итогам сравнения:

- `upstream/master...origin/master`:
  - 317 коммитов, которых нет в upstream
  - 263 коммита, которых нет в fork
- Изменений в файлах:
  - 1070 файлов изменены
  - примерно +320853 вставок, -40382 удалений

Это очень большой разрыв относительно исходной линии и свидетельствует о длительной кастомизации, а не о минимальном форке.

## 4. Основная идея: upstream vs fork

### Upstream (`glaretechnologies/substrata`)
Это более “чистый” open-source проект:

- generic Substrata metaverse; 
- класический engine/world project;
- базовые игровые/визуальные механики;
- open-source community-maintained baseline.

### Fork (`shipilovden/sub-metasiberia`)
Это уже проект, ориентированный на Metasiberia-specific product:

- другой branding и public identity;
- отдельный server-site/admin experience;
- отдельный веб-процесс/маркетинг/production-friendly setup;
- специализированные инструменты и контент для метавселенной;
- значительные UI, bot, avatar, integration и release improvements.

## 5. Что прямо видно по содержимому

### 5.1. Смена брендинга и продукта

Сравнение `README.md` показывает ключевую разницу:

- upstream README описывает Substrata как open-source metaverse, generic project;
- fork README уже говорит про Metasiberia как виртуальный мир “inspired by and based on Substrata”, с кастомными ссылками на `vr.metasiberia.com`, `metasiberia.com`, admin/signup.

Файлы:

- [README.md](../README.md)
- `upstream/master:README.md` (сравнение по git)

Это означает, что мета-проект уже стал отдельным продуктом на базе Substrata, а не просто родительской веткой.

### 5.2. Действительно мощный web/admin слой

В форке явно развита server-side web инфраструктура:

- `webserver/`
- `webserver_public_files/`
- `webserver_fragments/`
- `gui_client/AboutDialog.cpp` with Metasiberia links
- server/site login/admin flows

Важные ориентиры:

- [docs/FIGMA_SITE_SYNC.md](FIGMA_SITE_SYNC.md)
- [docs/METASIBERIA_WEBSITES.md](METASIBERIA_WEBSITES.md)
- [docs/SERVERS_AND_EXCHANGE.md](SERVERS_AND_EXCHANGE.md)

Это сильно отличается от базового upstream, где основа — движок и world runtime, а не production web portal.

### 5.3. Добавлены боты и AI-настройки

В релизных заметках прямо указаны пользовательские функции, которых в upstream нет в таком виде:

- редактор ботов в клиенте;
- создание/удаление/редактирование ботов через UI;
- AI settings: prompt, template variables, fallback, per-bot API key;
- support for Claude models;
- gesture slots, sound, use-actions, proximity responses;
- auto-look at nearest user;
- chat hearing radius and bot behaviors.

Файл-источник:

- [docs/RELEASE_NOTES_v0.0.21.md](RELEASE_NOTES_v0.0.21.md)

Это сильный признак того, что проект развивался не как “синхронизация с upstream”, а как добавление новой игровой/социальной функциональности под Metasiberia.

### 5.4. Avatar/Gear и inventory layer

В релизе выделены отдельные блоки:

- server infrastructure for Gear;
- `ObjectType_GearItem`;
- inventory UI and preview widget;
- equip/unequip sync and persistence;
- server capability gating and authoritative gear updates.

Это серьёзное расширение платформы, которое у upstream в таком виде не является базовым функционалом.

### 5.5. Map / portal / UI improvements

В форке есть уникальные добавления:

- OSM map world layer;
- priority download queue and map dock;
- portal editor improvements;
- theme menu and Qt themes;
- browser toolbar globe and favorites star;
- Russian localisation improvements.

Это типичные “product-specific UX” улучшения — часть проекта, а не чистое обновление движка.

### 5.6. XR runtime compatibility и stability work

Документы и release notes показывают, что fork ведёт активную работу в XR:

- companion mirror sync fixes during XR;
- script timers and XR startup improvements;
- upstream fixes from Substrata 1.7.1–1.7.3;
- local avatar visibility fix and XR-specific docs.

Файлы:

- [docs/VR_QT_INTEGRATION_PLAN_2026-03-20.md](VR_QT_INTEGRATION_PLAN_2026-03-20.md)
- [docs/XR_LOCAL_AVATAR_VISIBILITY_FIX_2026-03-27.md](XR_LOCAL_AVATAR_VISIBILITY_FIX_2026-03-27.md)
- [docs/XR_AVATAR_VISIBILITY_NOTE_2026-03-29.md](XR_AVATAR_VISIBILITY_NOTE_2026-03-29.md)
- [docs/XR_POSE_TRACE_ANALYSIS.md](XR_POSE_TRACE_ANALYSIS.md)

Это тоже указывает на форк, куда аккуратно подтягиваются upstream improvements и при этом сохраняются персональные XR-правки под Metasiberia.

### 5.7. Дополнительные кастомные системы, выходящие далеко за рамки upstream

Список ниже демонстрирует масштаб кастомизации и показывает, что fork не ограничивается ботами, сайтом и XR. Это уже проект с несколькими отдельными subsystem-level feature lines:

- система частиц;
- редактор объектов культуры;
- редактор научных объектов;
- аудио-плеер;
- усиленный чат;
- поддержка VR/XR;
- система скульптинга ландшафта;
- поддержка 3D Gaussian Splatting (3DGS);
- редактор вокселей;
- множество разных шрифтов в редакторе текста;
- система избранного;
- кнопка открывания локации в браузере;
- темы оформления;
- иконки и UI наборы;
- редактор анимаций;
- и это только часть, а не весь список.

Эти системы не выглядят как небольшие локальные настройки; они являются полноценными инструментами уровня редактора/контента/сцены. Для upstream Substrata характерен более базовый набор возможностей, тогда как Metasiberia развивается как более богатая платформа для креативных и production-процессов.

### 5.8. Факт-валидация по реальным файлам

Ниже — проверка по коду и документации, чтобы отделить “просто разговоры про кастомизацию” от реально присутствующих систем в репозитории.

| Функция / subsystem | Реальные доказательства в репозитории | Статус |
|---|---|---|
| Система частиц | `docs/changelog.txt` ("Added particle system - smoke, splashes etc."), `gui_client/ParticleEmitterSettings.h` и `gui_client/ParticleEmitterSettings.cpp` | Подтверждено |
| Поддержка VR / XR | `docs/VR_QT_INTEGRATION_PLAN_2026-03-20.md`, `docs/XR_LOCAL_AVATAR_VISIBILITY_FIX_2026-03-27.md`, `docs/XR_AVATAR_VISIBILITY_NOTE_2026-03-29.md`, `gui_client/XRSupport.*`, `gui_client/XRSession.*` | Подтверждено |
| Редактор научных объектов | `gui_client/ScientificObjectEditor.h`, `gui_client/ScientificObjectSettings.h`, `gui_client/MoleculeViewportWidget.*`, `docs/codex/verification-report.md` | Подтверждено |
| Аудиоплеер и radio streams | `docs/AUDIO_PLAYER_MUSIC_SETTINGS_2026-04-18.md`, `gui_client/ObjectEditor.cpp`, `shared/WorldObject.h` | Подтверждено |
| Усиленный чат / bot-сценарии | `docs/RELEASE_NOTES_v0.0.21.md`, `server/ChatBot.h` | Подтверждено как отдельный product layer |
| Скульптинг ландшафта | `gui_client/TerrainSystem.h` (`TerrainSculptTool`, `beginSculptStroke()`, `sculptAtWorld()`, `undoSculpt()`) | Подтверждено |
| 3D Gaussian Splatting (3DGS) | `gui_client/AddObjectDialog.cpp`, `gui_client/GaussianSplatRenderer.cpp`, `shared/GaussianSplatData.cpp`, `docs/codex/gaussian-splatting.md` | Подтверждено |
| Редактор вокселей | `gui_client/VoxelEditorPanel.h`, `gui_client/VoxelProceduralGenerator.h`, `docs/codex/verification-report.md` | Подтверждено |
| Кнопка открыть текущее местоположение в браузере | `docs/WEBMODE_BROWSER_BUTTON_2026-04-17.md`, `gui_client/MainWindow.cpp`, `gui_client/URLWidget.ui` | Подтверждено |
| Темы оформления / Qt themes | `docs/QT_THEMES_MENU_2026-04-17.md`, `resources/qt_themes/*.json`, `gui_client/MainWindow.cpp` | Подтверждено |
| Иконки и Lucide UI set | `docs/codex/verification-report.md` (Lucide, 49 SVG, icon mapping), `gui_client/LucideIconUtils.h` | Подтверждено |
| Редактор анимаций | `gui_client/AnimationEditorPanel.h`, `gui_client/AnimationEditorPanel.cpp` | Подтверждено |
| Система избранного / favorites / recents | `docs/codex/verification-report.md` (favorites, recents/search history), `docs/RELEASE_NOTES_v0.0.21.md` (favorites star) | Подтверждено |
| Культурные/контентные редакторы типов объектов | `gui_client/AddObjectDialog.cpp`, `gui_client/ObjectEditor.cpp`, `docs/codex/verification-report.md` (item categories: image/model, particles, voxels, tree, scientific/atom, audio, web, video, portal, bot) | Подтверждено в редакторной палитре |

Важно: в этом списке отдельные вещи подтверждены напрямую по коду, а некоторые — через release notes и verification report. Это уже серьёзно выходит за рамки обычного “upstream sync”, потому что Metasiberia в репозитории содержит целый слой редакторных/контентных систем поверх основного движка.

Дополнение по шрифтам: отдельного модульного “font editor” в repo я не нашёл как явного feature family, но есть реальное использование шрифтов/emoji-стека (`resources/fonts/sovietfont.woff`, `docs/CHAT_EMOJI_IMPLEMENTATION_2026-03-17.md`, `Segoe UI Emoji` в клиенте). То есть часть про шрифты подтверждается, но она выглядит скорее как платформенная поддержка текста/emoji, а не как отдельная большая subsystem story.

## 6. Сравнение по папкам

### `docs/`

В форке `docs/` сильно расширен и наполнен product-specific документацией:

- `FIGMA_SITE_SYNC.md`
- `METASIBERIA_WEBSITES.md`
- `SERVERS_AND_EXCHANGE.md`
- release notes v0.0.13–v0.0.22
- XR, portal, map, theme, audio-player, World Links and UI docs

Это уже не просто build docs, а проектное и product-management хранилище.

### `gui_client/`

Папка сильно отличается:

- unique Metasiberia UI and settings flows;
- bot editor dock;
- avatar settings and gear preview widgets;
- Qt desktop integration and Metasiberia-specific UI behavior;
- custom AboutDialog / branding integration.

### `webserver/` и `webserver_public_files/`

Это один из самых сильных evidence-points:

- server-side pages;
- auth/signup/admin logic;
- public-facing site assets;
- production/public-only pages and routing config.

Это типично для Metasiberia-as-platform, а не только “общей Substrata runtime”.

### `shared/`

Есть кастомные изменения в networking, serialization, versioning, object metadata and project-specific state.

### `resources/`

Ресурсный слой сильно различается — большое число файлов затронуто, что обычно означает:

- custom branding and UI assets;
- custom world content / server resources;
- stricter Metasiberia-specific deployment configuration.

## 7. Самые важные файлы и директории, отличающие Metasiberia от Stan­dard Substrata

Эти элементы особенно показательны:

1. `README.md`  
   Product rebranding and identity shift.

2. `docs/RELEASE_NOTES_v0.0.21.md`  
   Full feature release with Metasiberia-specific additions.

3. `docs/FIGMA_SITE_SYNC.md`  
   Separate design/site sync process for public site/admin walls.

4. `docs/METASIBERIA_WEBSITES.md`  
   Explicit separation of production websites.

5. `gui_client/BotEditorWidget.*`  
   Core bot editor functionality.

6. `gui_client/BotSettingsDialog.*`  
   Bot setup and AI config.

7. `webserver/` and `webserver_public_files/`  
   Metasiberia public/admin site layer.

8. `webserver/LoginHandlers.cpp` / `MainPageHandlers.cpp` / `WebServerRequestHandler.cpp`  
   Product-specific routing and site behavior.

9. `docs/VR_QT_INTEGRATION_PLAN_2026-03-20.md` and XR docs  
   XR-specific custom development from the fork.

10. `docs/MAP_WORLD_OSM_LAYER_2026-04-22.md` and related map docs  
   Custom map and world features.

11. `docs/QT_THEMES_MENU_2026-04-17.md` and similar UI docs  
   Branding and theme work specific to Metasiberia.

## 8. Доказательство, что это не чистый upstream sync

Ниже основные признаки, что это не просто синхронный форк:

- change magnitude: 1070 files changed
- release notes mention Metasiberia-specific features not found in generic Substrata
- separate website/admin/site rules and a dedicated docs cluster
- bot AI editor and inventory systems added
- production site integration with `vr.metasiberia.com` and `metasiberia.com`
- custom docs and product workflows around design, deployment, and server architecture

Если бы это был простой fork, мы бы ожидали в основном rename/branding and minimal custom feature layer. Здесь масштаб и глубина заметно выше.

## 9. Общая интерпретация

Короче говоря:

- upstream = базовый Substrata engine and world platform;
- fork = Metasiberia product layer built on top of Substrata;
- fork содержит множество уникальных функций, страниц, сервисов и release-логики;
- это уже не просто “наследование upstream”, а самостоятельная разработка на базе Substrata.

## 10. Итоговая формулировка

`sub-metasiberia` — это не точная копия `glaretechnologies/substrata`, а сильно кастомизированный downstream-проект. Основу он наследует от upstream, но в реальности это отдельный Metasiberia product stack: сервера, web/admin, branded site, bot AI systems, avatar gear, XR work и production deployment tooling.

Если нужно продолжить исследование, следующим логичным шагом будет:

1. сделать более детальный diff по папкам `gui_client`, `server`, `webserver`, `shared`, `resources`;
2. вынести список ключевых коммитов, которые добавляли custom функции;
3. составить таблицу “Что есть в upstream / чего нет в fork / что добавлено в Metasiberia”.
