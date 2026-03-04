# План структуры Wiki Metasiberia (RU)

Статус: проектный план страниц (подробный)
Дата: 2026-03-01
Цель: определить полную структуру wiki, закрыть пробелы и зафиксировать обязательные изображения для каждой страницы.

## 1) Как это будет выглядеть в wiki

- Визуальная структура wiki:
  - верхний уровень: `Home` + 6 тематических блоков (`Старт`, `Add`, `Редакторы`, `Миры/Web`, `Админка`, `Lua scripting`);
  - внутри каждого блока — пронумерованные страницы по сценариям;
  - навигация строится через `_Sidebar.md` (единое оглавление всей wiki).
- Главная `Home` — обзор и маршрут по разделам:
  - что такое Metasiberia;
  - быстрый старт;
  - переход к основным блокам документации.
- Формат каждой страницы (единый шаблон):
  - кратко: для чего страница;
  - prerequisites (если нужны права/роль/версия);
  - пошаговая инструкция;
  - проверка результата;
  - типичные ошибки и решение;
  - ссылки на связанные страницы.
- Визуальные материалы:
  - у каждой страницы обязательный комплект скриншотов (см. раздел 4);
  - скриншоты именуются одинаково по шаблону, чтобы навигация по медиа была предсказуемой.
- Состояние заполнения страниц:
  - план допускает пометку статуса: `planned`, `draft`, `ready`, `published`;
  - статус удобно отражать в начале страницы одной строкой.

## 2) Сколько страниц будет

План: **44 страницы** (включая `Home`).

- Home: 1
- Блок A. Старт и база: 8
- Блок B. Add-операции: 10
- Блок C. Редакторы и настройка: 8
- Блок D. Медиа/миры/web: 5
- Блок E. Админка: 6
- Блок F. Lua scripting: 6

## 3) Пробелы текущей публичной Wiki и как они закрываются этим планом

Важно: речь о состоянии **публичной GitHub Wiki на текущий момент** (фактически одна страница `Home`), а не о возможностях проекта в коде.

Ниже список пробелов и их покрытие в этом плане:

- Нет полноценного раздела по **Lua scripting**:
  - что не хватает: lifecycle скрипта, события/интеракции, HTTP в Lua, ограничения и безопасные практики;
  - чем закрываем: блок **F. Lua scripting** (страницы `50`–`55`).
- Слабо покрыты детально `Add`-сценарии (каждый тип отдельно):
  - чем закрываем: блок **B. Add-операции** (страницы `10`–`19`).
- Нет полного гайда по `Object Editor` и `Material Editor`:
  - чем закрываем: блок **C. Редакторы и настройки** (страницы `20`–`27`).
- Нет полного гайда по `Parcel Editor` и правам (`owner/admin/writer/all-writeable`):
  - чем закрываем: страница `24-Parcel-Editor-and-Rights.md` + связанные страницы блока C.
- Нет операционного admin-гайда по `/admin` (`users/parcels/transactions/map/flags/audit`):
  - чем закрываем: блок **E. Админка** (страницы `40`–`45`).
- Нет стандарта по изображениям:
  - чем закрываем: раздел `4) Стандарт изображений для всех страниц` + обязательные изображения для каждой страницы в разделе `5`.

## 4) Стандарт изображений для всех страниц

Обязательный минимум на каждую страницу:

1. `hero` — общий скрин темы страницы.
2. `step-1` — первый ключевой шаг.
3. `step-2` — второй ключевой шаг.
4. `result` — как выглядит успешный результат.
5. `error` — типовая ошибка или неправильный кейс (если применимо).

Рекомендуемая структура хранения:

- `docs/wiki/images/<page-slug>/hero.png`
- `docs/wiki/images/<page-slug>/step-1.png`
- `docs/wiki/images/<page-slug>/step-2.png`
- `docs/wiki/images/<page-slug>/result.png`
- `docs/wiki/images/<page-slug>/error.png`

Пример:

- `docs/wiki/images/19-add-camera-and-camera-screen/hero.png`

## 5) Подробный план страниц

## A. Старт и база (8)

1. `01-Install-Windows.md`
- Содержание: требования, загрузка инсталлятора, установка, первый запуск.
- Изображения: `hero`, `step-release-page`, `step-installer`, `result-installed`, `error-antivirus`.

2. `02-Registration-and-Login.md`
- Содержание: регистрация на сайте, подтверждение Terms, вход в клиенте, logout.
- Изображения: `hero`, `step-signup-form`, `step-terms-checkbox`, `result-logged-in`, `error-invalid-credentials`.

3. `03-First-Launch-and-Connection.md`
- Содержание: подключение к миру, индикаторы загрузки, базовая проверка управления.
- Изображения: `hero`, `step-connect`, `step-world-loaded`, `result-ready`, `error-connection`.

4. `04-Client-UI-Overview.md`
- Содержание: все меню, toolbar, dock-панели, назначение каждого блока.
- Изображения: `hero`, `step-menubar`, `step-toolbar`, `step-docks`, `result-layout`.

5. `05-Movement-and-Camera-Modes.md`
- Содержание: first/third person, fly mode, Go actions.
- Изображения: `hero`, `step-fly-mode`, `step-third-person`, `result-navigation`, `error-stuck-camera`.

6. `06-Graphics-Audio-Mic-Webcam-Settings.md`
- Содержание: качество, звук, микрофон, вебкамера, базовая оптимизация.
- Изображения: `hero`, `step-options`, `step-audio`, `step-mic-webcam`, `result-stable-fps`.

7. `07-Troubleshooting-Startup-and-Login.md`
- Содержание: частые ошибки запуска/входа, чеклист диагностики.
- Изображения: `hero`, `error-login`, `error-network`, `error-resource-load`, `result-fixed`.

8. `08-FAQ-Quick-Answers.md`
- Содержание: короткие ответы на частые вопросы новичка.
- Изображения: `hero`, `step-faq-nav`, `result-faq-usage`.

## B. Add-операции (10)

9. `10-Add-Model-Image.md`
- Содержание: Add Model/Image (library/disk/web), форматы, preview, create.
- Изображения: `hero`, `step-add-dialog`, `step-from-disk`, `step-from-web`, `result-object-created`.

10. `11-Add-Video.md`
- Содержание: локальное/URL видео, autoplay/loop/muted, проверка воспроизведения.
- Изображения: `hero`, `step-add-video`, `step-video-url`, `result-video-screen`, `error-video-load`.

11. `12-Add-Web-View.md`
- Содержание: создание webview, target URL, типовые use-cases.
- Изображения: `hero`, `step-add-webview`, `step-target-url`, `result-webview-live`, `error-webview-empty`.

12. `13-Add-Audio-Source.md`
- Содержание: выбор mp3/wav, volume, loop/autoplay, spatial звук.
- Изображения: `hero`, `step-add-audio`, `step-audio-file`, `result-audio-playing`, `error-audio-missing`.

13. `14-Add-Voxels.md`
- Содержание: создание voxel group, базовый редактинг.
- Изображения: `hero`, `step-add-voxels`, `step-edit-voxels`, `result-voxel-object`.

14. `15-Add-Hypercard-and-Text.md`
- Содержание: гиперкарта и текст, content/target URL.
- Изображения: `hero`, `step-add-hypercard`, `step-add-text`, `result-text-card`.

15. `16-Add-Spotlight-and-Lighting.md`
- Содержание: spotlight, углы конуса, luminous flux, цвет.
- Изображения: `hero`, `step-add-spotlight`, `step-light-settings`, `result-light-scene`.

16. `17-Add-Seat-and-Avatar-Interaction.md`
- Содержание: seat, поза, ограничения протокола и поведение аватара.
- Изображения: `hero`, `step-add-seat`, `step-seat-angles`, `result-avatar-seated`.

17. `18-Add-Portal-and-Navigation.md`
- Содержание: portal, позиционирование, переходы.
- Изображения: `hero`, `step-add-portal`, `step-portal-target`, `result-portal-use`.

18. `19-Add-Camera-and-Camera-Screen.md`
- Содержание: camera+screen, source UID, FOV/resolution/FPS, производительность.
- Изображения: `hero`, `step-add-camera`, `step-link-screen`, `result-stream-screen`, `error-protocol-version`.

## C. Редакторы и настройки (8)

19. `20-Object-Editor-Basics.md`
- Содержание: transform, script, content, target URL, snap/grid.
- Изображения: `hero`, `step-transform`, `step-snap-grid`, `result-object-edited`.

20. `21-Material-Editor-and-Textures.md`
- Содержание: color/texture/normal/metallic/roughness/opacity/emission, flags.
- Изображения: `hero`, `step-material-select`, `step-texture-slots`, `step-emission`, `result-material-look`.

21. `22-Lightmaps.md`
- Содержание: bake fast/high, статус, remove lightmap.
- Изображения: `hero`, `step-bake-fast`, `step-bake-hq`, `result-lightmap`, `error-bake-failed`.

22. `23-Physics-Settings.md`
- Содержание: collidable/dynamic/sensor, mass, friction, restitution, COM offset.
- Изображения: `hero`, `step-physics-flags`, `step-mass-friction`, `result-physics-behavior`.

23. `24-Parcel-Editor-and-Rights.md`
- Содержание: owner/admin/writer, all-writeable, mute outside audio, spawn.
- Изображения: `hero`, `step-owner-writers`, `step-spawn`, `step-geometry`, `result-parcel-updated`.

24. `25-World-Settings-Terrain-Water.md`
- Содержание: terrain sections, maps, section width, water/default z.
- Изображения: `hero`, `step-new-section`, `step-height-mask`, `step-water`, `result-terrain-applied`.

25. `26-Environment-Sun-and-Northern-Lights.md`
- Содержание: sun theta/phi, local sun, northern lights.
- Изображения: `hero`, `step-sun-theta-phi`, `step-northern-lights`, `result-environment-look`.

26. `27-Object-Ops-Copy-Paste-Clone-Delete-Search.md`
- Содержание: копирование, вставка, clone, delete, find by ID/nearby.
- Изображения: `hero`, `step-copy-paste`, `step-clone`, `step-find-object`, `result-ops-done`.

## D. Медиа, миры, веб (5)

27. `30-Photo-Mode-and-Upload.md`
- Содержание: photo mode, скриншоты, upload (server/Telegram/VK).
- Изображения: `hero`, `step-photo-mode`, `step-capture`, `step-upload`, `result-posted`.

28. `31-Vehicles-and-Gestures.md`
- Содержание: summon vehicles, gestures UI, частые проблемы.
- Изображения: `hero`, `step-vehicle-menu`, `step-summon`, `step-gesture-ui`, `result-vehicle-gesture`.

29. `32-Personal-World-Create-and-Edit.md`
- Содержание: personal world, create/edit world, права и ограничения.
- Изображения: `hero`, `step-create-world`, `step-edit-world`, `result-world-ready`.

30. `33-Web-Account-Chatbots-News-Events-Photos.md`
- Содержание: account, chatbots, news, events, photos user-flow.
- Изображения: `hero`, `step-account`, `step-new-chatbot`, `step-create-event`, `result-content-published`.

31. `34-WebClient-and-Visit-Links.md`
- Содержание: `/webclient`, `/visit`, шаринг ссылок, ограничения.
- Изображения: `hero`, `step-open-webclient`, `step-visit-link`, `result-web-visit`.

## E. Админка (6)

32. `40-Admin-Quickstart.md`
- Содержание: вход в `/admin`, обзор секций, базовый workflow.
- Изображения: `hero`, `step-admin-home`, `step-admin-nav`, `result-admin-ready`.

33. `41-Admin-Users-and-Permissions.md`
- Содержание: users, user page, gardener/dyn tex flags, reset password.
- Изображения: `hero`, `step-admin-users`, `step-admin-user`, `step-reset-password`, `result-user-updated`.

34. `42-Admin-Parcels-Root-and-World.md`
- Содержание: root/world parcels, owner/editors/geometry, bulk actions.
- Изображения: `hero`, `step-admin-parcels`, `step-world-parcels`, `step-bulk-actions`, `result-parcel-admin`.

35. `43-Admin-Auctions-Orders-Transactions.md`
- Содержание: auctions/orders/eth-transactions, статусы, модерация.
- Изображения: `hero`, `step-auctions`, `step-orders`, `step-transactions`, `result-state-updated`.

36. `44-Admin-Map-LOD-and-Web-Assets.md`
- Содержание: map regen/recreate, LOD chunks, web assets status/reload.
- Изображения: `hero`, `step-admin-map`, `step-admin-lod`, `step-web-assets-reload`, `result-map-status`.

37. `45-Admin-Feature-Flags-ReadOnly-Audit.md`
- Содержание: feature flags, read-only mode, audit log, safe operations.
- Изображения: `hero`, `step-feature-flags`, `step-read-only`, `step-audit-log`, `result-flags-applied`.

## F. Lua scripting (6)

38. `50-Lua-Scripting-Intro.md`
- Содержание: где хранится script, как включить, минимальный hello-script.
- Изображения: `hero`, `step-open-script-editor`, `step-first-script`, `result-script-running`.

39. `51-Lua-Object-Script-Lifecycle.md`
- Содержание: жизненный цикл скрипта объекта, инициализация/обновление.
- Изображения: `hero`, `step-lifecycle-diagram`, `step-init-update`, `result-lifecycle-ok`.

40. `52-Lua-Events-and-Interactions.md`
- Содержание: реакции на взаимодействия, события объектов/игроков.
- Изображения: `hero`, `step-event-bind`, `step-interaction-test`, `result-event-fired`.

41. `53-Lua-HTTP-and-External-Integrations.md`
- Содержание: HTTP из Lua, зависимость от feature flag, ограничения.
- Изображения: `hero`, `step-http-flag`, `step-http-script`, `result-http-response`, `error-http-disabled`.

42. `54-Lua-Safety-Performance-and-Debugging.md`
- Содержание: лимиты, безопасность, логирование, отладка и anti-patterns.
- Изображения: `hero`, `step-log-output`, `step-performance-check`, `error-script-timeout`, `result-stable-script`.

43. `55-Lua-Recipes-Examples.md`
- Содержание: готовые рецепты (дверь, кнопка, триггер, чат-реакция, таймер).
- Изображения: `hero`, `recipe-door`, `recipe-trigger`, `recipe-timer`, `result-recipes`.

## 6) Главная страница `Home` (1)

44. `Home.md`
- Содержание: краткий обзор проекта, быстрый старт, ссылки на все разделы.
- Изображения: `hero`, `quickstart`, `navigation-map`.

---

## 7) Порядок внедрения (рекомендуемый)

Фаза 1:
- Обновить `Home.md`, `_Sidebar.md`.
- Выпустить блок A (08 страниц).

Фаза 2:
- Выпустить блок B + C (18 страниц).

Фаза 3:
- Выпустить блок D + E (11 страниц).

Фаза 4:
- Выпустить блок F (Lua) + финальный QA всех страниц и изображений.

---

## 8) Definition of Done для каждой страницы

- Страница создана и добавлена в `_Sidebar.md`.
- Все команды/пути проверены на актуальность.
- Добавлены обязательные изображения по шаблону.
- Есть блок "Проверка результата".
- Есть блок "Типичные ошибки".
- Есть дата обновления и версия клиента/сервера (если важно).

---

Конец плана.
