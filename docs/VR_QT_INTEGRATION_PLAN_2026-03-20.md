# ПЛАН ИНТЕГРАЦИИ VR В QT-КЛИЕНТ METASIBERIA

Дата: 2026-03-20  
Область: Qt-native клиент (`USE_SDL=OFF`), без WebXR.

## 1. Цель

Интегрировать desktop VR в существующий Qt-клиент Metasiberia так, чтобы:

- не ломать текущий desktop/non-VR режим;
- сохранить существующий Qt UI, dock-окна и editor workflow;
- встроить XR как дополнительный runtime-режим, а не как отдельный клиент с новой архитектурой;
- подготовить платформенную основу для дальнейшей синхронизации головы и рук по сети.

## 2. Что уже есть в проекте

- Qt/OpenGL клиент уже имеет устойчивый render loop через `GlWidget` и `MainWindow::timerEvent()`.
- В проекте уже есть пространственное аудио и обновление head transform.
- В рендере уже есть инфраструктура камеры и дополнительные render-pass.
- Ветка `feature/qt-openxr-integration` уже умеет собирать Qt-клиент с `XR_SUPPORT=ON` и локальным OpenXR SDK.
- В `gui_client/XRSession.*` уже есть базовый OpenXR backend-слой: runtime probe, `xrCreateInstance`, `xrGetSystem`, `xrGetOpenGLGraphicsRequirementsKHR`, `xrCreateSession`, `xrCreateReferenceSpace`, `xrPollEvent`, `xrBeginSession`, `xrWaitFrame`, `xrBeginFrame`, `xrLocateViews`, stereo swapchains, per-eye OpenGL render target binding, `XrCompositionLayerProjection` submit и `xrEndFrame` для текущего Win32 Qt/OpenGL контекста.
- `GUIClient` уже делает один XR bootstrap после GL init, пишет подробный статус в лог и показывает XR-блок в diagnostics.

## 3. Что нужно скачать и установить

Для реальной desktop VR-интеграции потребуется следующее:

1. OpenXR SDK для разработки.
   - Нужны заголовки OpenXR и loader library для native-сборки.
   - В репозиторий SDK сейчас не вендорен.

2. OpenXR runtime на машине, где будет запускаться клиент.
   - Подойдёт любой активный desktop runtime, совместимый с вашим headset.
   - Типичные варианты: SteamVR OpenXR runtime, Windows Mixed Reality OpenXR runtime, Meta/Oculus OpenXR runtime.

3. Qt дополнительно ставить не нужно сверх уже используемой native Qt-сборки.
   - Текущая база Qt/OpenGL у проекта уже есть.

4. Для первого этапа не нужны дополнительные сетевые библиотеки.
   - Мы стартуем с local viewer mode, без сетевой передачи поз рук.

## 4. Принципы интеграции

- Qt остаётся shell-слоем: окно, dock'и, editor UI, меню, status bar.
- XR не должен размазываться по `MainWindow`-логике.
- Основная XR-логика должна жить в `gui_client` / `OpenGLEngine`-уровне.
- `GlWidget` используется как bridge к текущему GL lifecycle и fallback preview, но не как место, где должна сидеть вся OpenXR-логика.
- Фича должна быть выключена по умолчанию и собираться только через явный флаг.

## 5. Поэтапный план

## Этап 0. Подготовка инфраструктуры сборки

Цель:

- добавить compile-time флаг для XR;
- подготовить нейтральный слой `XRSupport`, не ломающий текущий клиент;
- зафиксировать архитектурный план и зависимости.

Статус:

- завершено в текущей ветке.

## Этап 1. Build-time интеграция OpenXR SDK

Цель:

- подключить OpenXR headers / loader в CMake только для native сборок;
- не затрагивать web/emscripten путь;
- не включать XR автоматически в обычных релизных сборках.

Изменения:

- добавить поиск SDK path через CMake cache/env var;
- добавить include/lib пути только при `XR_SUPPORT=ON`;
- линковать OpenXR loader только в native Qt build.

Риск:

- минимальный, если оставить всё за compile-time флагом.

Статус:

- завершено в текущей ветке.

## Этап 2. XR runtime/session слой

Цель:

- ввести `XRSession` / `XRDevice` abstraction;
- реализовать lifecycle: create instance, select system, create session, begin/end frame;
- подготовить eye pose / view / projection данные на кадр.

Изменения:

- новый модуль в `gui_client`;
- без вмешательства в сетевой протокол;
- без принудительного изменения основного non-VR пути.

Статус:

- частично завершено;
- реализованы `xrCreateInstance -> xrGetSystem -> xrGetOpenGLGraphicsRequirementsKHR -> xrCreateSession -> xrCreateReferenceSpace`;
- реализованы `xrPollEvent`, session state transitions, `xrBeginSession/xrWaitFrame/xrBeginFrame/xrLocateViews/xrEndFrame`;
- реализованы OpenXR swapchains, per-eye render target binding и `XrCompositionLayerProjection` submit через существующий `OpenGLEngine`;
- в Qt-клиент добавлен отдельный XR render/submit этап на живом GL context перед companion-window draw;
- companion preview в `GlWidget` сейчас может переотрисовываться по mirror-view из XR, без прямого blit из OpenXR swapchain в Qt framebuffer;
- запрос API version на `xrCreateInstance` зафиксирован на `OpenXR 1.0` для совместимости с desktop runtime, которые ещё не принимают `XR_CURRENT_API_VERSION` заголовков SDK;
- tracking-space теперь якорится по first-person anchor и world heading для позиции, а ориентация головы дополнительно выравнивается по горизонтальному взгляду в момент `FOCUSED`, чтобы нейтральная посадка HMD не уводила взгляд в пол;
- app reference space теперь выбирается через runtime capability probe с приоритетом `STAGE` и fallback на `LOCAL`, чтобы SteamVR/Business Streaming не уводили Qt-клиент в `TrackingUniverseSeated` только из-за долгого запроса `LOCAL`;
- при валидной HMD pose Qt-клиент теперь использует фактическую world-space позу головы для outbound avatar transform и listener/audio alignment, чтобы взгляд и положение головы аватара совпадали с шлемом без изменения позиционного anchor-а OpenXR runtime;
- добавлен ручной XR recenter на клавишу `Home`, чтобы пользователь мог заново взять корректный ноль уже после того, как шлем надет и смотрит прямо.
- для аппаратной диагностики Qt-клиент теперь пишет `xr_pose_trace.csv` в `%APPDATA%/Cyberspace`, где каждые ~100 мс сохраняются raw и corrected head pose, reference space и текущие camera/avatar angles.

## Этап 3. Stereo render в существующем рендере

Цель:

- рендерить два глаза через существующий `OpenGLEngine`;
- использовать текущую поддержку camera transform и lens shift / off-axis frustum;
- сохранить mirror output в окне Qt.

Изменения:

- добавить per-eye camera setup;
- добавить XR frame submit path;
- окно Qt продолжает жить как companion/mirror window.

Главный риск:

- `QOpenGLWidget/QGLWidget` управляет своим lifecycle и framebuffer semantics, поэтому swapchain-взаимодействие XR надо аккуратно отделить от обычного widget draw.

## Этап 4. Input для VR

Цель:

- добавить controller poses;
- реализовать teleport / pointer / basic interaction;
- постепенно уменьшить зависимость от mouse-only screen-space interaction.

Изменения:

- слой XR input actions;
- преобразование контроллеров в world-space ray interaction.

Статус:

- частично завершено;
- в `XRSession` добавлен backend-only action subsystem без вмешательства в `PlayerPhysicsInput` и desktop gameplay input;
- созданы `grip pose` / `aim pose` actions для левой и правой руки;
- добавлены базовые input actions для `select` и `trigger value` с suggested bindings для common OpenXR controller profiles;
- в frame loop добавлены `xrSyncActions`, `xrGetCurrentInteractionProfile`, `xrGetActionState*` и `xrLocateSpace` для hand poses;
- в diagnostics теперь выводятся состояние action subsystem, текущие interaction profiles, trigger/select state и активность/валидность `grip` / `aim` pose по каждой руке;
- teleport / pointer / gameplay mapping пока сознательно не подключались.

## Этап 5. Social VR

Цель:

- расширить сетевой протокол отдельными pose для головы и рук;
- обновить аватарный рендер и репликацию.

Это отдельный этап, не нужен для первого локального VR MVP.

## 6. Что уже сделано на этой ветке

В текущем состоянии ветки уже сделан безопасный groundwork и первый session bootstrap:

- добавлен compile-time флаг `XR_SUPPORT`;
- добавлен базовый `XRSupport` модуль;
- подключён OpenXR SDK в CMake только для native Qt build;
- добавлен `XRSession` с bootstrap под текущий Win32 OpenGL context;
- добавлен базовый OpenXR frame lifecycle с `xrLocateViews` на каждом кадре;
- добавлен backend-only слой OpenXR actions/hand poses с диагностикой по левой/правой руке;
- обычный non-VR путь клиента остаётся рабочим даже при неудачном XR bootstrap.

## 7. Практический порядок следующих шагов

1. Подтвердить работу `grip` / `aim` pose и trigger/select diagnostics на целевых runtime/headset.
2. Привязать head/controller tracking к gameplay/input слою без зависимости от desktop camera semantics.
3. Реализовать world-space pointer / teleport поверх уже существующего action backend.
4. При необходимости оптимизировать companion preview с переотрисовки на прямой texture/FBO blit.
5. Затем уже расширять сетевую часть под social VR.

## 8. Ограничения первого этапа

- Web client в этот план не входит.
- Android в этот план не входит.
- Сетевые позы рук не входят в первый этап.
- World-space VR UI лучше делать после базового head-tracked/stereo MVP.

## 9. XR Alignment Note

- Для нейтрального взгляда и head alignment клиент теперь должен опираться на отдельный OpenXR `VIEW` space pose, а не на ориентацию первого глаза из `xrLocateViews`, чтобы центр взгляда в шлеме совпадал с реальным бинокулярным направлением, а не только с desktop mirror левого глаза.
- XR recenter теперь должен ждать стабильную позу шлема на нормальной высоте головы в `STAGE` space, чтобы не калиброваться по шлему, лежащему на столе или низко над полом.
- Stereo render в OpenXR должен использовать прямую per-eye projection matrix из `xrLocateViews(...).fov` без дополнительной viewport/lens-аппроксимации desktop-камеры, иначе в HMD возможны эффект "приближения" и неверный горизонт при внешне нормальном mirror-окне.
- Companion mirror в Qt-окне тоже должен использовать тот же XR projection override, что и HMD, чтобы desktop preview не скрывал ошибку, которая видна только в шлеме.
- Для SteamVR/OpenXR на VIVE Business Streaming swapchain extent лучше держать ниже compositor-порога downsampling; если runtime советует слишком большой квадратный размер, безопаснее клампить его до совместимого диапазона, чем рендерить в HMD-картинку, которую compositor потом пересобирает по своему пути.
- Для SteamVR/OpenXR на VIVE Business Streaming swapchain format тоже нужно выбирать консервативно: `GL_RGBA16F` и даже `GL_SRGB8_ALPHA8` могут пройти через `xrCreateSwapchain`, но затем отвергаться compositor-слоем при `ComposeLayerProjection`, поэтому безопаснее предпочитать обычные color-renderable форматы (`GL_RGBA8`, `GL_RGB10_A2`, `GL_RGBA16`) и оставлять более экзотические варианты только как крайний fallback.
- Цветные OpenXR swapchain-ресурсы для HMD должны создаваться не только с `XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT`, но и с `XR_SWAPCHAIN_USAGE_SAMPLED_BIT`, потому что compositor затем сам читает их как текстуры; без этого некоторые runtime'ы принимают swapchain, но отвергают submit уже на этапе `ComposeLayerProjection`.
- XR runtime-реализация должна обрабатывать `XrEventDataReferenceSpaceChangePending` для активного `STAGE/LOCAL` space и запускать повторный head-alignment после runtime recenter, иначе SteamVR/Business Streaming может продолжить показывать мир уже из нового origin, пока клиент всё ещё живёт на старой калибровке.

## 10. XR Locomotion Note

- The Qt/OpenXR client now has a first connected `teleport locomotion` path in `GUIClient`, wired on top of the existing OpenXR action backend instead of through desktop-only input code.
- Hold the controller `trigger` or `select` action to show the world-space teleport beam and landing marker.
- A green landing marker means the hit surface is walkable and the teleport target is valid.
- A blue landing marker means the current hit is invalid, or the ray reached only a non-walkable surface / empty end point.
- Releasing the same controller button performs the teleport by moving the player capsule in physics space, so XR camera placement stays driven by the tracked HMD pose instead of an artificial camera offset.
