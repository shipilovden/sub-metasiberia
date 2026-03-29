# План интеграции Cesium в Metasiberia

Дата: 2026-03-27

Статус: исследование завершено, кодовая интеграция еще не начата

## 1. Цель

Нужно подготовить интеграцию нового окна `Cesium` в меню `Window` рядом с `World Settings`, при этом:

- сохранить текущую архитектуру dock-окон Qt;
- не ломать существующий heightmap-based terrain;
- подготовить основу для подключения Cesium Native и geospatial landscape;
- сделать будущий UI в стиле существующего `World Settings`;
- заложить безопасный путь для дальнейшей синхронизации client/server/world settings.

Главный вывод по исследованию:

- текущий terrain в Metasiberia локальный, секционный и основан на heightmap EXR;
- Cesium работает в другой модели: geospatial, tileset-driven, с глобальными координатами, стримингом и LOD;
- поэтому Cesium нельзя корректно врезать как "еще одно поле Height map URL";
- нужен отдельный `Cesium` dock, отдельный набор настроек и отдельный runtime provider.

## 2. Что изучено в локальном коде

Изучены следующие части проекта:

- `gui_client/MainWindow.cpp`
- `gui_client/MainWindow.h`
- `gui_client/MainWindow.ui`
- `gui_client/WorldSettingsWidget.cpp`
- `gui_client/WorldSettingsWidget.h`
- `gui_client/WorldSettingsWidget.ui`
- `gui_client/TerrainSpecSectionWidget.cpp`
- `gui_client/TerrainSpecSectionWidget.h`
- `gui_client/TerrainSpecSectionWidget.ui`
- `gui_client/GUIClient.cpp`
- `gui_client/GUIClient.h`
- `gui_client/TerrainSystem.cpp`
- `gui_client/TerrainSystem.h`
- `shared/WorldSettings.cpp`
- `shared/WorldSettings.h`
- `docs/FOG_WORLD_SETTINGS_INTEGRATION_2026-03-10.md`

Также изучены официальные материалы Cesium:

- Cesium Native reference docs: `https://cesium.com/learn/cesium-native/ref-doc/`
- Cesium Native Developer Setup Guide: `https://cesium.com/learn/cesium-native/ref-doc/developer-setup.html`
- Google Photorealistic 3D Tiles learn page: `https://cesium.com/learn/photorealistic-3d-tiles-learn/`
- Cesium ion SDK page: `https://cesium.com/platform/cesiumjs/ion-sdk/`
- Cesium ion REST API docs: `https://cesium.com/learn/ion/rest-api/`

## 3. Как сейчас устроено меню Window

Важно: пункты `Window` в текущем клиенте не описаны полностью вручную в `.ui`.

Базовая часть меню задается в:

- `gui_client/MainWindow.ui`

Но список dock-окон добавляется программно в:

- `gui_client/MainWindow.cpp`

Через `toggleViewAction()` для каждого `QDockWidget`.

Фактическая схема сейчас такая:

1. В `.ui` существуют сами dock widgets.
2. В `MainWindow::initialiseUI()` они инициализируются.
3. Там же их `toggleViewAction()` добавляются в `ui->menuWindow`.
4. Поэтому любой новый dock логично подключать по тому же шаблону.

Это важно для `Cesium`, потому что правильная интеграция должна выглядеть так:

1. новый `QDockWidget` в `MainWindow.ui`
2. новый внутренний виджет, например `CesiumSettingsWidget`
3. инициализация в `MainWindow::initialiseUI()`
4. `ui->menuWindow->addAction(ui->cesiumDockWidget->toggleViewAction())`
5. стабильный `objectName`, чтобы работали `saveState()` / `restoreState()`

## 4. Как сейчас устроен World Settings

`World Settings` уже реализован именно как dock-окно, а не как отдельный modal dialog.

Ключевые точки:

- dock: `worldSettingsDockWidget`
- содержимое dock: `WorldSettingsWidget`
- сигнал из виджета: `settingsChangedSignal()`
- слот в `MainWindow`: `worldSettingsChangedSlot()`
- передача в runtime: `GUIClient::worldSettingsChangedFromUI(...)`

### 4.1. Что делает MainWindow

`MainWindow`:

- вызывает `ui->worldSettingsWidget->init(this);`
- подписывается на `settingsChangedSignal()`
- умеет обновлять UI из пришедших world settings
- умеет блокировать редактирование, если у пользователя нет прав

### 4.2. Что делает WorldSettingsWidget

`WorldSettingsWidget`:

- получает `WorldSettings` через `setFromWorldSettings(...)`
- собирает `WorldSettings` обратно через `toWorldSettings(...)`
- динамически пересоздает список секций terrain
- умеет добавлять и удалять terrain section
- при `Apply` поднимает сигнал наверх

### 4.3. Что видно по стилю текущего UI

Текущий стиль `World Settings` такой:

- форма с вертикальным scroll area;
- секционные блоки;
- поля URL и `Browse`;
- числовые контролы;
- явная кнопка `Apply`;
- внизу дополнительные группы `Sun` и `Fog`;
- редактируемость зависит от прав пользователя.

Это означает, что новое окно `Cesium` должно быть похоже по UX:

- тоже dock widget;
- тоже form-like layout;
- тоже scrollable;
- тоже с `Apply`;
- тоже с read-only режимом для неавторизованных пользователей.

## 5. Как сейчас устроен ландшафт в Metasiberia

Текущая система terrain вообще не является geospatial.

Она основана на локальных секциях terrain и наборе heightmap/detail map ресурсов.

### 5.1. Модель данных

В `shared/WorldSettings.h` есть:

- `TerrainSpecSection`
- `TerrainSpec`
- `WorldSettings`

`TerrainSpecSection` содержит:

- `x`
- `y`
- `heightmap_URL`
- `mask_map_URL`
- `tree_mask_map_URL`

`TerrainSpec` содержит:

- список `section_specs`
- detail color maps
- detail height maps
- `terrain_section_width_m`
- `terrain_height_scale`
- `default_terrain_z`
- `water_z`
- terrain flags

`WorldSettings` содержит:

- `terrain_spec`
- параметры солнца
- параметры тумана
- прочие world-level данные

### 5.2. Как terrain попадает в клиент

В `GUIClient::updateGroundPlane()` происходит:

1. чтение `connected_world_settings.terrain_spec`
2. преобразование URL-ориентированного terrain spec в path-ориентированный `TerrainPathSpec`
3. загрузка heightmap / mask / detail texture ресурсов через `ResourceManager`
4. создание и инициализация `TerrainSystem`

### 5.3. Что важно архитектурно

Текущая terrain pipeline предполагает:

- локальные Cartesian координаты;
- конечное число секций terrain;
- heightmap EXR как базу рельефа;
- пересоздание terrain system при изменении world settings;
- отсутствие встроенной geodetic модели мира.

Это очень хорошо работает для текущего мира Metasiberia, но это не та же самая модель, которую использует Cesium.

## 6. Почему Cesium нельзя интегрировать как "еще один terrain section"

Cesium Native ориентирован на:

- 3D Tiles runtime streaming
- selection / culling / LOD
- глобальные ellipsoid-based координаты
- geospatial transforms
- отдельный renderer bridge
- отдельный asset/network pipeline

То есть Cesium landscape:

- не равен одному EXR-файлу;
- не равен просто списку terrain sections;
- не обязан иметь те же координатные допущения, что текущий `TerrainSystem`;
- может быть привязан к WGS84, ECEF, local horizontal frame и streamed tiles.

Именно поэтому прямое встраивание Cesium внутрь текущего `TerrainSpec` как набора еще нескольких URL-полей было бы ошибкой.

## 7. Что важно из официальной архитектуры Cesium

По официальным материалам Cesium Native:

- `Cesium3DTilesSelection` отвечает за runtime streaming, LOD, culling, cache management и decode 3D Tiles.
- `CesiumGeospatial` содержит geospatial math, ellipsoid transforms и связанные типы.
- `CesiumIonClient` отвечает за работу с Cesium ion account / assets / API.
- `CesiumRasterOverlays` нужен для imagery overlays поверх terrain / tiles.
- `CesiumQuantizedMeshTerrain` относится к terrain в формате quantized-mesh.

Ключевые классы и понятия, которые придется учитывать в нативной интеграции:

- `Cesium3DTilesSelection::Tileset`
- `Cesium3DTilesSelection::TilesetExternals`
- `Cesium3DTilesSelection::IPrepareRendererResources`
- `Cesium3DTilesSelection::ViewState`
- `CesiumGeospatial::LocalHorizontalCoordinateSystem`
- `CesiumIonClient`

Вывод:

- для Metasiberia нужен не только UI слой;
- нужен полноценный runtime bridge между Cesium Native и нашим OpenGL/scene graph/asset lifecycle.

## 8. Что важно из документации по установке Cesium Native

По официальному `Developer Setup Guide` Cesium Native требует:

- Visual Studio 2019+ или GCC 11+ или Clang 12+
- CMake 3.15+
- локальный `vcpkg`
- `VCPKG_ROOT`
- желательно `nasm` для лучшей JPEG decoding performance

На Windows официальный путь сборки выглядит через:

- `vcpkg`
- `cmake --preset=vcpkg-windows-vs`
- сборку Debug / Release

Для Metasiberia это означает:

- сначала надо проверить, как аккуратнее заводить Cesium как third-party dependency в наш существующий CMake;
- не надо сразу ставить его в основное дерево без отдельного под-плана;
- лучше сначала сделать dependency integration plan, потом уже тянуть исходники / submodule / external build.

На момент этого документа установка Cesium в репозиторий еще не выполнялась.

## 9. Что означает Google Photorealistic 3D Tiles для нашей задачи

Google Photorealistic 3D Tiles:

- это streamed geospatial 3D Tiles content;
- это не замена только рельефа, а полноценный georeferenced 3D tiles dataset;
- требует корректной geospatial frame integration;
- скорее ближе к отдельному content provider, чем к расширению существующего terrain section UI.

Следовательно, если пользователь говорит, что в окне `Cesium` должен задаваться "ландшафт", надо трактовать это шире:

- terrain provider
- tileset provider
- imagery / overlay provider
- geospatial origin

А не только как "подставить еще одну карту высот".

## 10. Рекомендуемая архитектура для Metasiberia

Рекомендация: не смешивать Cesium с текущим `TerrainSpec` на первом этапе.

Правильнее завести отдельный блок настроек, например:

- `CesiumWorldSettings`

И уже затем встроить его в:

- либо `WorldSettings`
- либо временно в отдельный client-side settings object на этапе прототипа

### 10.1. Предлагаемая структура данных

Минимально полезный будущий `CesiumWorldSettings`:

- `enabled`
- `provider_mode`
- `ion_asset_id`
- `ion_access_token`
- `tileset_url`
- `raster_overlay_url`
- `origin_latitude_deg`
- `origin_longitude_deg`
- `origin_height_m`
- `maximum_screen_space_error`
- `enable_collision`
- `enable_water_sync`

Возможные `provider_mode`:

- `Disabled`
- `CesiumWorldTerrain`
- `GooglePhotorealistic3DTiles`
- `Custom3DTilesURL`

### 10.2. Почему это лучше, чем расширять TerrainSpec

Потому что `TerrainSpec` сейчас семантически означает:

- секционный локальный heightmap terrain

А Cesium означает:

- geospatial streamed world source

Это две разные модели данных.

## 11. Как должен выглядеть новый dock Cesium

Новый dock должен повторять UX-подход `World Settings`, но не копировать его поля.

### 11.1. Предлагаемый UI состав

Заголовок dock:

- `Cesium`

Поля:

- `Enabled`
- `Provider`
- `Ion asset ID`
- `Ion access token`
- `Custom tileset URL`
- `Raster overlay URL`
- `Origin latitude`
- `Origin longitude`
- `Origin height`
- `Maximum screen-space error`
- `Enable collision`
- `Apply`

Опционально второй блок:

- `Debug draw bounding volumes`
- `Show tile credits`
- `Suspend streaming`

### 11.2. По стилю

Рекомендуемый стиль совпадает с `World Settings`:

- `QDockWidget`
- внутренний `QWidget`
- форма на `QGridLayout` / `QVBoxLayout`
- `QScrollArea`
- кнопка `Apply`
- `updateControlsEditable()`

### 11.3. По интеграции в Window menu

Новый пункт в `Window` должен появиться так же, как текущие dock-окна:

1. добавить `cesiumDockWidget` в `MainWindow.ui`
2. добавить `CesiumSettingsWidget`
3. в `MainWindow::initialiseUI()` добавить `ui->menuWindow->addAction(ui->cesiumDockWidget->toggleViewAction())`

### 11.4. Обязательное требование к стартовой geospatial точке

Нужно зафиксировать отдельное продуктовое требование:

- при первом переключении мира на Cesium стартовый geospatial origin / начальный spawn должен указывать на район Тайжины, Кемеровская область, по предоставленной пользователем Google Maps ссылке;
- в качестве дефолтной стартовой точки для первой реализации можно использовать координаты центра вида из ссылки: `53.6917967, 87.4326242`;
- это должно трактоваться именно как начальная точка мира / начальный spawn для Cesium-режима, а не как произвольный пример;
- при этом система обязательно должна поддерживать переопределение этой точки на любые другие координаты.

Из этого следуют требования к UI:

- в окне `Cesium` должны быть явные поля для задания `Origin latitude`, `Origin longitude` и `Origin height`;
- желательно предусмотреть отдельную кнопку или будущий helper workflow для вставки Google Maps URL с извлечением координат;
- дефолтное значение должно быть предзаполнено точкой Тайжины, но пользователь должен иметь возможность задать любое другое пространство вручную.

## 12. Что придется сделать в runtime

Вот где начинается настоящая сложность.

Для реальной Cesium integration понадобится:

### 12.1. Новый provider abstraction

Нужно ввести абстракцию уровня:

- `GroundProvider`
- или `LandscapeProvider`

Чтобы клиент умел работать хотя бы с двумя типами:

- текущий `TerrainSystem` heightmap provider
- будущий `Cesium` provider

Без этого мы получим спутанный код в `GUIClient::updateGroundPlane()`.

### 12.2. Renderer bridge

Cesium Native не рендерит "сам себя" в наш движок.

Нужен мост, который реализует поведение, аналогичное:

- `IPrepareRendererResources`

и связывает:

- tile content
- glTF / mesh / texture upload
- lifecycle GPU ресурсов
- освобождение ресурсов

с нашим OpenGL / scene representation.

### 12.3. Geospatial transform layer

Нужно решить, как связывать:

- WGS84 / ECEF / local horizontal coordinates Cesium

с:

- текущей локальной системой координат Metasiberia

Без этого нельзя надежно расположить:

- аватара
- world objects
- камеры
- physics
- interactions

### 12.4. World origin strategy

Почти наверняка понадобится локальный origin strategy:

- выбрать geospatial anchor
- вокруг него строить local frame
- переводить camera pose и possibly object positions между local world и Cesium frame

Это критичная часть дизайна.

## 13. Рекомендуемый порядок внедрения

Ниже безопасный поэтапный путь.

### Этап 1. UI-каркас без Cesium runtime

Сделать:

- новый `Cesium` dock
- `CesiumSettingsWidget`
- пункт в `Window`
- локальный `Apply`
- read-only gating как в `World Settings`

На этом этапе никакой реальной tileset загрузки еще не делаем.

### Этап 2. Модель настроек

Сделать:

- `CesiumWorldSettings`
- временное хранение в клиенте
- либо постоянное в `WorldSettings` с версионированием

Если добавляем в `WorldSettings`, то потребуется:

- bump serialisation version
- обновить read/write код
- проверить серверную совместимость

### Этап 3. Provider abstraction

Вытащить terrain lifecycle из прямой логики `GUIClient::updateGroundPlane()` в более чистую абстракцию.

Цель:

- не смешивать EXR terrain и Cesium runtime в одном большом `if`.

### Этап 4. Third-party dependency integration

Только после этого:

- подтянуть Cesium Native
- определить способ включения в CMake
- решить стратегию `vcpkg`
- проверить совместимость с нашими Windows/Linux build trees

### Этап 5. Минимальный Cesium runtime prototype

Первый прототип должен уметь только:

- поднять tileset
- задать origin
- обновлять `ViewState` от камеры
- рисовать tiles

Без server sync и без попытки сразу покрыть все сценарии.

### Этап 6. Совмещение с существующим миром

Затем надо решить:

- Cesium заменяет terrain целиком
- или Cesium живет параллельно terrain
- или Cesium доступен только в worlds с отдельным provider mode

Это продуктово и архитектурно важное решение.

### Этап 7. Server sync и persistence

После proof-of-concept:

- синхронизировать Cesium settings через world settings
- добавить контроль прав
- добавить загрузку/сохранение на сервере

## 14. Риски

### 14.1. Архитектурный риск

Самый большой риск в том, что Cesium - это не "новая текстура", а новый пространственный и стриминговый слой.

### 14.2. Риск координат

Если неправильно спроектировать origin / transform layer, то начнут плыть:

- камера
- физика
- привязка объектов
- коллизии
- сетевое согласование позиций

### 14.3. Риск CMake / dependency management

Cesium Native тянет ощутимую зависимостную экосистему через `vcpkg`.

Нельзя врезать это без отдельной проверки на:

- Windows
- Linux
- будущий Android impact

### 14.4. Риск смешивания UI и runtime

Если начать с runtime без чистой модели настроек и без отдельного provider abstraction, `GUIClient.cpp` быстро станет слишком хрупким.

## 15. Рекомендуемое решение на сейчас

На текущем этапе я рекомендую следующий курс:

1. Не трогать пока действующий `WorldSettingsWidget`.
2. Не встраивать Cesium поля в существующий `TerrainSpec`.
3. Сначала сделать отдельный `Cesium` dock в стиле `World Settings`.
4. Под него ввести отдельный `CesiumWorldSettings`.
5. Сразу заложить в `CesiumWorldSettings` дефолтный стартовый geospatial origin для района Тайжины и возможность его полного ручного переопределения.
6. Затем отдельно подготовить runtime provider abstraction.
7. Только после этого подтягивать Cesium Native как dependency и делать минимальный streamed tiles prototype.

Это даст самую безопасную интеграцию с минимальным риском сломать текущий terrain.

## 16. Что делать следующим практическим шагом

Следующий правильный шаг после этого документа:

- сделать чистый UI skeleton для `Cesium` dock без включения Cesium Native;

или, если нужен еще один подготовительный этап:

- сначала оформить dependency integration plan для Cesium Native и проверить, как его безопаснее собирать в наших build trees.

Оба варианта разумны, но для стабильности проекта я бы шел в таком порядке:

1. `Cesium` dock skeleton
2. `CesiumWorldSettings` model
3. provider abstraction
4. Cesium Native dependency integration
5. runtime prototype

## 17. Краткий итог

Исследование подтверждает:

- окно `Cesium` нужно делать отдельным dock-окном в `Window`;
- по стилю оно должно быть похоже на `World Settings`;
- по архитектуре оно не должно быть просто расширением текущего EXR terrain UI;
- для корректной интеграции потребуется отдельная модель настроек, geospatial layer и runtime provider;
- подключение Cesium Native надо делать только после подготовки UI и архитектурной прослойки.
