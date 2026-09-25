# Портал документации Metasiberia

Назначение: единая точка входа во все основные знания проекта и классификация historical/legacy материалов.

Проверено: 2026-07-10. Рабочий код имеет приоритет; status документа не доказывает live production.

## Как пользоваться порталом

1. Новая сессия начинает с [корневого AGENTS.md](../../AGENTS.md), затем открывает [Codex guide](README.md).
2. Инженерная задача выбирает маршрут в [project-index.md](project-index.md) и при необходимости использует [orchestration](ORCHESTRATION.md).
3. Факты берутся из профильного документа и при необходимости проверяются минимальным source range.
4. MCOS определяет стандарт знаний, но не заменяет факты: [MCOS.md](MCOS.md).

## Статусы

| Статус | Значение |
| --- | --- |
| основной | canonical current entry |
| актуальный | подтверждённый тематический документ |
| partial/WIP | часть реализована или active working tree |
| plan | намерение, не доказательство реализации |
| historical | сохраняется как хронология |
| obsolete/superseded | не использовать как current instruction |
| unknown | требует проверки владельца/runtime |

## Уровни инженерной документации

| Уровень | Назначение | Основные документы |
| --- | --- | --- |
| A. Конституция | Универсальные инженерные правила и поведение Codex. | [MCOS.md](MCOS.md) |
| B. Архитектура | Current structure, process/data boundaries, dependencies. | [system-overview.md](system-overview.md), [architecture.md](architecture.md), [component-relations.md](component-relations.md) |
| C. Навигация | Быстрый route к нужному owner, термину или документу. | [project-index.md](project-index.md), [project-map.md](project-map.md), [documentation-index.md](documentation-index.md), [search-guide.md](search-guide.md), [glossary.md](glossary.md) |
| D. Правила разработки | Практический workflow, token policy, orchestration и safe change rules. | [development-rules.md](development-rules.md), [token-policy.md](token-policy.md), [ORCHESTRATION.md](ORCHESTRATION.md), [TESTING_AND_ACCEPTANCE.md](TESTING_AND_ACCEPTANCE.md), [build-and-test.md](build-and-test.md) |
| E. Специализированные знания | Подсистемы и WIP, где нужен отдельный owner. | [data-map.md](data-map.md), [scientific-object-editor.md](scientific-object-editor.md), [scientific-data-providers.md](scientific-data-providers.md), [voxel-editor.md](voxel-editor.md), [inventory-system.md](inventory-system.md) |
| F. История и долг | ADR, changelog, verification, audit snapshots, known debt. | [decisions.md](decisions.md), [documentation-changelog.md](documentation-changelog.md), [verification-report.md](verification-report.md), [audit-report.md](audit-report.md), [engineering-debt.md](engineering-debt.md) |

Документы разных уровней не должны смешивать роли: history объясняет почему, current docs объясняют как устроено сейчас, MCOS объясняет как работать.

## Основная база docs/codex

| Документ | Главный вопрос | Статус |
| --- | --- | --- |
| [system-overview.md](system-overview.md) | Что такое Metasiberia и какие surfaces существуют? | основной |
| [project-index.md](project-index.md) | С какого документа/owner начать задачу? | основной |
| [project-map.md](project-map.md) | Где находятся подсистемы и physical owners? | основной |
| [architecture.md](architecture.md) | Как устроены layers, targets, processes и contracts? | основной |
| [component-relations.md](component-relations.md) | Кто producer/consumer и какие dependencies? | основной |
| [data-map.md](data-map.md) | Какие данные/configs существуют и где хранятся? | основной |
| [glossary.md](glossary.md) | Что означает cross-component термин? | основной glossary |
| [scientific-object-editor.md](scientific-object-editor.md) | Что реально реализовано в Scientific Object WIP? | основной WIP |
| [scientific-data-providers.md](scientific-data-providers.md) | Какие научные источники/API/форматы и provider rules использовать? | provider architecture reference |
| [cultural-object-editor-research.md](cultural-object-editor-research.md) | Возможно ли и как спроектировать редактор объектов культуры? | research/architecture proposal |
| [native-editors-stage-status-2026-07-18.md](native-editors-stage-status-2026-07-18.md) | Что уже входит в первый этап Cultural/animation/photo/documents/MCP и где проходят его границы? | partial/WIP implementation status |
| [единая-игровая-система-план.md](единая-игровая-система-план.md) | План единой игровой, образовательной и экономической системы | research specification |
| [cad-промышленные-объекты-план.md](cad-промышленные-объекты-план.md) | План CAD, инженерных чертежей и промышленных объектов | research specification |
| [voxel-editor.md](voxel-editor.md) | Как устроены native voxel tools/layers/clipboard/generators и какие shared-format границы остаются? | основной WIP |
| [inventory-system.md](inventory-system.md) | Как устроены native Gear Inventory, avatar attachment/preview и client/server contract? | active working tree; production не развёрнут |
| [current-state.md](current-state.md) | Что baseline, partial, WIP, plan или unknown? | основной |
| [decisions.md](decisions.md) | Почему приняты устойчивые architecture/policy decisions? | основной ADR |
| [engineering-debt.md](engineering-debt.md) | Какие известные проблемы требуют отдельной задачи? | основной debt register |
| [development-rules.md](development-rules.md) | Как безопасно изменять проект? | основной |
| [search-guide.md](search-guide.md) | Как найти минимальную область? | основной |
| [token-policy.md](token-policy.md) | Как выбирать глубину и переиспользовать знания? | основной |
| [build-and-test.md](build-and-test.md) | Какие команды/проверки подтверждены и рискованны? | основной; команды не исполнялись в Phase 2 |
| [qt-build-policy.md](qt-build-policy.md) | Как различать Qt 5 `master` и Qt 6 `qt6-integration` workflow? | основной policy |
| [audit-report.md](audit-report.md) | Что проверено и какие gaps остались? | основной snapshot |
| [documentation-index.md](documentation-index.md) | Где находится единый portal документации? | основной portal |
| [documentation-changelog.md](documentation-changelog.md) | Что менялось в knowledge system? | основной журнал |
| [verification-report.md](verification-report.md) | Чем завершались крупные knowledge/documentation tasks? | основной отчёт |
| [MCOS.md](MCOS.md) | Как организуются знания и работа Codex? | MCOS, canonical engineering constitution |

## Оркестрация и acceptance

| Документ | Главный вопрос | Статус |
| --- | --- | --- |
| [README.md](README.md) | С чего начать и какие документы открыть по задаче? | основной entry |
| [ORCHESTRATION.md](ORCHESTRATION.md) | Когда и как Orchestrator использует логические роли и параллелизм? | основной policy |
| [DELEGATION_CONTRACT.md](DELEGATION_CONTRACT.md) | Как поставить дочернему агенту ограниченную и проверяемую задачу? | основной template |
| [REVIEW_PROTOCOL.md](REVIEW_PROTOCOL.md) | Когда нужен review и как сообщать findings? | основной gate |
| [TESTING_AND_ACCEPTANCE.md](TESTING_AND_ACCEPTANCE.md) | Как выбирать verification и считать задачу готовой? | основной gate |

## AGENTS loaders

| Loader | Уникальная область |
| --- | --- |
| [AGENTS.md](../../AGENTS.md) | repository entry, hierarchy, minimum workflow |
| [gui_client/AGENTS.md](../../gui_client/AGENTS.md) | Qt/SDL/Web/XR/editor and Scientific WIP rules |
| [server/AGENTS.md](../../server/AGENTS.md) | realtime/state/persistence/concurrency |
| [webserver/AGENTS.md](../../webserver/AGENTS.md) | embedded routes/Server Website/Admin |
| [shared/AGENTS.md](../../shared/AGENTS.md) | protocol/model/serialization |
| [webclient/AGENTS.md](../../webclient/AGENTS.md) | Emscripten shell/generated output |
| [scripts/AGENTS.md](../../scripts/AGENTS.md) | side-effect classes/build/release/ops |
| [docs/AGENTS.md](../AGENTS.md) | status/history/Wiki/documentation rules |

AGENTS содержат только routing и обязательные local rules. Архитектурные объяснения принадлежат docs/codex.

## Canonical first-party documents

| Область | Документ | Статус/роль |
| --- | --- | --- |
| Public entry | [README.md](../../README.md) | публичное описание; architecture details subordinate to docs/codex |
| Production/server | [SERVERS_AND_EXCHANGE.md](../SERVERS_AND_EXCHANGE.md) | operational runbook + dated history; live-check before action |
| Public Websites | [METASIBERIA_WEBSITES.md](../METASIBERIA_WEBSITES.md) | canonical boundary between `metasiberia.com` static site and `vr.metasiberia.com` server site; REG.RU workflow |
| Release | [RELEASE_PIPELINE.md](../RELEASE_PIPELINE.md) | canonical manual release policy |
| Security reporting | [SECURITY.md](../SECURITY.md) | vulnerability reporting, не full security design |
| Chat | [CHAT_REDESIGN_PLAN_2026-06-30.md](../CHAT_REDESIGN_PLAN_2026-06-30.md) | canonical requirements + partial implementation plan |
| Figma/site | [FIGMA_SITE_SYNC.md](../FIGMA_SITE_SYNC.md) | Server Website/Figma workflow |
| Web Client | [WEBCLIENT_METASIBERIA.md](../WEBCLIENT_METASIBERIA.md) | implementation notes + historical rollout |
| XR | [VR_QT_INTEGRATION_PLAN_2026-03-20.md](../VR_QT_INTEGRATION_PLAN_2026-03-20.md) | mixed plan/implementation status |
| XR diagnostics | [XR_POSE_TRACE_ANALYSIS.md](../XR_POSE_TRACE_ANALYSIS.md) | current diagnostic guide |
| Map | [MAP_WORLD_OSM_LAYER_2026-04-22.md](../MAP_WORLD_OSM_LAYER_2026-04-22.md) | superseded direct-OSM history with current warning |
| Resources | [IMAGE_RESOURCE_RECOVERY_2026-03-21.md](../IMAGE_RESOURCE_RECOVERY_2026-03-21.md) | current recovery/fallback behavior |
| World links | [WORLD_LINKS_METASIBERIA.md](../WORLD_LINKS_METASIBERIA.md) | `/world`/`/visit` routes |

## Feature documents: current or mixed

| Документ | Классификация |
| --- | --- |
| [AUDIO_PLAYER_MUSIC_SETTINGS_2026-04-18.md](../AUDIO_PLAYER_MUSIC_SETTINGS_2026-04-18.md) | implemented feature note |
| [BOT_UI_PLAN.md](../BOT_UI_PLAN.md) | mixed implementation journal/plan; field/runtime recheck |
| [CAMERA_WORLD_STREAM_IMPLEMENTATION_PLAN_2026-02-27.md](../CAMERA_WORLD_STREAM_IMPLEMENTATION_PLAN_2026-02-27.md) | plan partially overtaken by code |
| [CESIUM_INTEGRATION_PLAN_2026-03-27.md](../CESIUM_INTEGRATION_PLAN_2026-03-27.md) | plan |
| [FOG_WORLD_SETTINGS_INTEGRATION_2026-03-10.md](../FOG_WORLD_SETTINGS_INTEGRATION_2026-03-10.md) | implemented note |
| [GESTURE_SETTINGS.md](../GESTURE_SETTINGS.md) | current local persistence semantics |
| [PORTAL_EDITOR_SETTINGS_2026-04-19.md](../PORTAL_EDITOR_SETTINGS_2026-04-19.md) | implemented note |
| [QT_THEMES_MENU_2026-04-17.md](../QT_THEMES_MENU_2026-04-17.md) | implemented theme/QSettings note |
| [WEBMODE_BROWSER_BUTTON_2026-04-17.md](../WEBMODE_BROWSER_BUTTON_2026-04-17.md) | implemented host/mode note |
| [UI_RUNTIME_TRANSLATION_2026-04-18.md](../UI_RUNTIME_TRANSLATION_2026-04-18.md) | historical implementation note |
| [UI_TRANSLATION_HOTFIX_2026-04-18.md](../UI_TRANSLATION_HOTFIX_2026-04-18.md) | historical hotfix |

Root font journals [FONT_SELECTION_FEATURE.md](../../FONT_SELECTION_FEATURE.md) и [FONT_SELECTION_IMPLEMENTATION.md](../../FONT_SELECTION_IMPLEMENTATION.md) сохраняются как mixed/historical evidence; current code/build имеет приоритет.

## Superseded и historical

- [IMAGE_RESOURCE_BASIS_FALLBACK_2026-03-23.md](../IMAGE_RESOURCE_BASIS_FALLBACK_2026-03-23.md) — superseded host workaround.
- [QT6_MIGRATION.md](../QT6_MIGRATION.md) — historical/unfinished migration; Qt 5 canonical.
- [XR_AVATAR_VISIBILITY_NOTE_2026-03-29.md](../XR_AVATAR_VISIBILITY_NOTE_2026-03-29.md) и [XR_LOCAL_AVATAR_VISIBILITY_FIX_2026-03-27.md](../XR_LOCAL_AVATAR_VISIBILITY_FIX_2026-03-27.md) — incident history; current XR invariants имеют приоритет.
- [CHAT_EMOJI_IMPLEMENTATION_2026-03-17.md](../CHAT_EMOJI_IMPLEMENTATION_2026-03-17.md) — rollout history.
- Release notes `v0.0.13`–`v0.0.21` — immutable release history; current tagged note: [v0.0.21](../RELEASE_NOTES_v0.0.21.md).
- `local_backups/*.md`, `emscripten_build*` — archive/generated evidence, не current instruction.

## Wiki

| Документ | Роль |
| --- | --- |
| [wiki/README.md](../wiki/README.md) | docs-as-code/publish rules |
| [wiki/Home.md](../wiki/Home.md) | Wiki home |
| [wiki/_Sidebar.md](../wiki/_Sidebar.md), [wiki/_Footer.md](../wiki/_Footer.md) | GitHub Wiki special files; preserve names |
| [wiki/01-Install-Windows.md](../wiki/01-Install-Windows.md) | onboarding draft |
| [wiki/02-Registration-and-Login.md](../wiki/02-Registration-and-Login.md) | onboarding draft |
| [wiki/03-First-Launch-and-Connection.md](../wiki/03-First-Launch-and-Connection.md) | onboarding draft |
| [wiki/WIKI-PAGES-PLAN-RU.md](../wiki/WIKI-PAGES-PLAN-RU.md) | roadmap, partially implemented |

Temporary capability plans и orphan `Footer.md` считаются legacy/duplicate до отдельной Wiki cleanup; special files не переименовывать.

## Non-Markdown references

| Путь | Роль |
| --- | --- |
| `docs/building.txt` | detailed upstream build history; paths/versions recheck |
| `docs/running server.txt` | generic legacy server guide |
| `docs/fuzzing.txt` | manual historical fuzz setup |
| `docs/changelog.txt` | upstream/product history |
| `docs/licence.txt` | aggregated licensing text; preserve format |

## Исключено из first-party maintenance

Third-party/vendored documentation (например `secp256k1-master/README.md`) не правится как часть Metasiberia documentation migration, если задача не относится к dependency.

## Правила сопровождения портала

- Добавлять документ только если у него уникальный вопрос/owner.
- Перед созданием документа проверить, нельзя ли расширить существующий canonical home.
- При смене роли/status обновлять эту страницу и [documentation-changelog.md](documentation-changelog.md).
- Не перечислять каждый source file/class.
- Не заменять relative links absolute local paths.
- Plans/historical docs не удалять без анализа references; помечать current replacement.
- Runtime/production facts датировать и не считать вечными.
# Product identity

- [Substrata → Metasiberia migration matrix](PRODUCT_IDENTITY_MIGRATION.md)
