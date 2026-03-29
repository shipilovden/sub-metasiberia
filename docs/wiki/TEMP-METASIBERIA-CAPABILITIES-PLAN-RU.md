# Metasiberia: Временный план возможностей для Wiki (черновик)

Статус: черновик для проверки (не финальная wiki)
Дата: 2026-03-01
Охват: аудит возможностей GUI-клиента и web/admin для планирования wiki

## 1. Цель

Этот документ — временный master-checklist для будущих wiki-страниц.
Цель: зафиксировать ключевые пользовательские и административные возможности по порядку, от установки и регистрации до продвинутых сценариев редактирования и администрирования.

## 2. Что было проверено (исходники кода)

- Меню/действия клиента:
  - `gui_client/MainWindow.ui`
  - `gui_client/MainWindow.cpp`
  - `gui_client/MainWindow.h`
- In-world HUD/UI:
  - `gui_client/GestureUI.cpp`
  - `gui_client/PhotoModeUI.cpp`
- Редактирование объектов и материалов:
  - `gui_client/ObjectEditor.ui`
  - `gui_client/ObjectEditor.cpp`
  - `gui_client/MaterialEditor.ui`
  - `gui_client/MaterialEditor.cpp`
- Инструменты parcel/world/environment:
  - `gui_client/ParcelEditor.ui`
  - `gui_client/ParcelEditor.cpp`
  - `gui_client/WorldSettingsWidget.ui`
  - `gui_client/WorldSettingsWidget.cpp`
  - `gui_client/TerrainSpecSectionWidget.ui`
  - `gui_client/EnvironmentOptionsWidget.ui`
  - `gui_client/EnvironmentOptionsWidget.cpp`
- Поток добавления модели:
  - `gui_client/AddObjectDialog.ui`
  - `gui_client/AddObjectDialog.cpp`
- Web и admin-роуты:
  - `webserver/WebServerRequestHandler.cpp`
  - `webserver/AdminHandlers.cpp`

## 3. Сквозная карта пользовательского пути (high-level)

1. Установить клиент.
2. Запустить клиент и подключиться к серверу/миру.
3. Зарегистрироваться или войти.
4. Перемещаться по миру (first person, fly, third person).
5. Создавать и редактировать контент (модели/изображения/webview/video/audio/voxels/text и т.д.).
6. Управлять правами и настройками parcel.
7. Управлять материалами/текстурами/lightmap.
8. Использовать медиа/соц-инструменты (photo mode, upload).
9. Использовать личный мир и web-инструменты аккаунта.
10. Для админов: использовать `/admin` и связанные страницы для модерации и операций.

---

## 4. Карта UI клиента (верхний уровень)

### 4.1 Строка меню

- `Edit`
- `Movement`
- `Avatar`
- `Vehicles`
- `View`
- `Go`
- `Tools`
- `Window`
- `Help`

### 4.2 Быстрые действия на toolbar

- `Add Model / Image`
- `Add Video`
- `Add Hypercard`
- `Add Voxels`
- `Add Web View`
- `Show Parcels`

### 4.3 Основные dock-панели

- Chat
- Webcam
- Editor (Object/Parcel editors)
- Help Information
- Material Browser
- Indigo View
- Diagnostics
- Environment
- World Settings

---

## 5. Возможности по меню

## 5.1 Edit

- Undo / Redo
- Создание контента:
  - Add Model / Image
  - Add Hypercard
  - Add Text
  - Add Spotlight
  - Add Camera
  - Add Seat
  - Add Audio Player
  - Add Web View
  - Add Video
  - Add Decal
  - Add Portal
- Избранное:
  - Add to Favorites
- Операции с объектами:
  - Copy Object
  - Paste Object
  - Clone Object
  - Delete Object
  - Find Object by ID
  - Find Objects Nearby
- Lightmaps:
  - Bake lightmaps (fast/high quality) for parcel
- Дисковые операции:
  - Save Object To Disk
  - Save Parcel Objects To Disk
  - Load Object(s) From Disk

## 5.2 Movement

- Fly Mode (toggle)

## 5.3 Avatar

- Avatar Settings

## 5.4 Vehicles

- Summon Bike
- Summon Hovercar
- Summon Boat
- Summon Jet Ski
- Summon Car

## 5.5 View

- Third Person Camera (toggle)

## 5.6 Go

- Go Back
- Go to Position
- Go to Parcel
- Go To Start Location
- Go to Main World
- Go to Personal World (использует username залогиненного пользователя)
- Go to Cryptovoxels world
- Go to Substrata / Metasiberia / Shki-nvkz server
- Go to Favorites (submenu)
- Set current location as start location

## 5.7 Tools

- Take Screenshot
- Show Screenshot Folder
- Show Log
- Export view to Indigo
- Mute Audio
- Options

## 5.8 Window

- Reset Layout
- Enter Fullscreen
- Language: English / Russian

## 5.9 Help

- About Metasiberia
- Update

---

## 6. Действия "Add" и точное поведение

## 6.1 Add Model / Image

- Вкладки диалога:
  - Model library (встроенные примитивы/ассеты: Quad, Cube, Capsule, Cylinder, Icosahedron, Platonic_Solid, Torus)
  - From disk (OBJ, GLTF, GLB, VOX, STL, IGMESH; JPG, PNG, GIF, EXR, KTX, KTX2)
  - From Web (URL)
- Есть preview и проверки загрузки текстур.
- Перед созданием проверяются права записи в parcel.

## 6.2 Add Hypercard

- Создает `ObjectType_Hypercard` с редактируемым текстом/content.
- Ориентируется по направлению игрока.
- Проверяет права parcel.

## 6.3 Add Text

- Создает `ObjectType_Text` с текстом по умолчанию.
- Ставит default-материал с поддержкой двусторонности/альфы.

## 6.4 Add Spotlight

- Создает `ObjectType_Spotlight` с параметрами конуса и emitting material.

## 6.5 Add Camera

- Требует активного подключения к серверу.
- Protocol gate: версия протокола сервера должна быть `>= 50`.
- Создает пару: `ObjectType_Camera` + `ObjectType_CameraScreen`.
- Автоматически ставит workflow для связки camera/screen.
- По умолчанию задает render size/FOV/max FPS и enabled-флаги.

## 6.6 Add Seat

- Protocol gate: версия протокола сервера должна быть `>= 49`.
- Создает `ObjectType_Seat` с default-углами конечностей.

## 6.7 Add Portal

- Создает `ObjectType_Portal` в позиции впереди игрока, выровненной по земле.

## 6.8 Add Web View

- Создает `ObjectType_WebView`.
- URL по умолчанию: `https://vr.metasiberia.com/`.

## 6.9 Add Video

- Источник: локальный файл или URL.
- URL видео сохраняется в emission texture материала.
- Default-флаги: autoplay + loop.

## 6.10 Add Audio Player

- Выбор одного или нескольких `.mp3/.wav`, копирование в resources и сборка плейлиста.
- Создает panel-объект audio player на базе `WebView`, а не красную generic-капсулу.
- Плейлист хранится в `content` (по одному URL на строку), а сам panel UI выполнен как компактный transport-bar только с progress bar и кнопками `Shuffle`, `Prev`, `Play/Pause`, `Next`, `Repeat`.
- Редактирование плейлиста перенесено в editor: добавление треков, добавление URL, удаление и перестановка треков.
- Даже если у legacy/битого объекта временно пустой `target_url`, audio player должен восстанавливаться по playlist-content и снова показываться как panel, а не как generic webview/cube.
- Треки `.mp3/.wav` раздаются в плеер с корректными audio MIME-типами, чтобы воспроизведение не обрывалось после старта.

## 6.11 Add Decal

- Создает non-collidable generic decal-объект.
- Использует decal-флаг материала.

## 6.12 Add Voxels

- Создает `ObjectType_VoxelGroup` со стартовым voxel.

---

## 7. Возможности редактирования объектов (Object Editor)

## 7.1 Базовые transform/script/content

- Model URL/file
- Position x/y/z
- Scale x/y/z + linked scaling
- Rotation z/y/x
- Toggle 3D gizmo controls
- Snap to grid + grid spacing
- Script text
- Content field
- Target URL field

## 7.2 Materials

- Выбор материала из нескольких
- Создание нового материала
- Полная интеграция Material Editor

## 7.3 Поля Material Editor

- Colour
- Texture URL
- Texture scale x/y
- Roughness
- Metallic fraction
- Metallic-roughness texture
- Normal map
- Opacity
- Emission colour
- Emission texture
- Luminance (nits)
- Флаги:
  - Hologram
  - Use vert colours for wind
  - Double sided
  - Decal

## 7.4 Lightmaps

- Показ lightmap URL
- Удаление lightmap
- Bake lightmap (fast / high quality)
- Статус bake

## 7.5 Physics

- Collidable / Dynamic / Sensor
- Mass
- Friction
- Restitution
- Center of mass offset

## 7.6 Audio group

- Audio file URL/path
- Volume
- Audio autoplay
- Audio loop

## 7.7 Video group

- Video URL
- Video volume
- Video autoplay/loop/muted

## 7.8 Spotlight group

- Luminous flux
- Light colour
- Cone start/end angles

## 7.9 Seat group

- Upper/lower leg angles
- Upper/lower arm angles

## 7.10 Camera group

- Enabled
- FOV Y
- Near distance
- Far distance
- Render width
- Render height
- Max FPS

## 7.11 Camera Screen group

- Enabled
- Source camera UID
- Material index

## 7.12 Видимость групп по типу объекта

Редактор переключает видимость групп в зависимости от object type:
- Generic
- Hypercard
- Voxel Group
- Spotlight
- Web View
- Video
- Text
- Portal
- Seat
- Camera
- Camera Screen

---

## 8. Возможности Parcel Editor

- Parcel ID / Created time
- Owner (ссылка на пользователя по id/name)
- Title / Description
- Списки Admins
- Списки Writers
- All Writeable
- Mute outside audio
- Spawn point x/y/z
- Отображение границ parcel (min/max)
- Редактирование геометрии parcel:
  - Position x/y/z
  - Size x/y/z + linked scaling
- Ссылка "Show parcel on web"
- Permission-aware режимы редактирования:
  - basic fields
  - owner + geometry
  - member lists

---

## 9. Редактирование world/environment

## 9.1 World Settings

- Список terrain sections в scroll-area
- Создание/удаление terrain section
- Поля секции:
  - section x, y
  - Height map URL
  - Mask map URL
  - Tree mask map URL
- Глобальные поля terrain:
  - detail color maps 0..3
  - detail height map 0
  - terrain section width
  - default terrain Z
  - water Z
  - water enabled
- Применение world settings update

## 9.2 Environment Options

- Use local sun direction
- Sun angle from vertical (theta)
- Sun azimuth (phi)
- Northern Lights toggle

---

## 10. Возможности in-world HUD

Из `GestureUI` и `PhotoModeUI`:

- Панель жестов:
  - открыть/скрыть
  - запуск/остановка gesture-анимаций
  - UI управления жестами
- Быстрое меню транспорта:
  - summon bike/car/boat/jetski/hovercar
- Photo mode:
  - toggle photo mode
  - слайдеры камеры/постобработки (focus, blur, EV, saturation, focal length, roll)
  - upload UI
- Toggle микрофона + индикатор уровня микрофона
- Toggle/кнопка webcam (Qt dock visibility или capture toggle в зависимости от сборки)

---

## 11. Регистрация/логин/аккаунт и возможности personal world

На стороне клиента:
- Login dialog и logout action
- Signup dialog
- Go to Personal World (маршрутизация мира по username залогиненного пользователя)

Web-side роуты (пользовательские потоки):
- Auth/account:
  - `/login`, `/signup`, `/logout_post`
  - `/reset_password`, `/change_password` и связанные POST handlers
  - `/account`, `/account_gestures`
- Страницы мира/пользователя:
  - `/world/<name>`
  - `/create_world`, `/create_world_post`
  - `/edit_world/<name>`, `/edit_world_post`
- Chatbots:
  - `/new_chatbot`, `/create_new_chatbot_post`
  - `/edit_chatbot`, `/edit_chatbot_post`
  - `/delete_chatbot_post`
- Photos/events/news:
  - `/photos`, `/photo/<id>`, `/edit_photo_parcel`, `/delete_photo_post`
  - `/events`, `/create_event`, `/edit_event`, `/event/<id>`
  - `/news`, `/news_post/<id>`

---

## 12. Возможности admin-панели (web)

Основные admin-страницы навигации:
- `/admin`
- `/admin_users`, `/admin_user/<id>`
- `/admin_parcels`
- `/admin_world_parcels`, `/admin_world_parcel`
- `/admin_parcel_auctions`, `/admin_parcel_auction/<id>`
- `/admin_orders`, `/admin_order/<id>`
- `/admin_sub_eth_transactions`, `/admin_sub_eth_transaction/<id>`
- `/admin_map`
- `/admin_news_posts`
- `/admin_lod_chunks`
- `/admin_worlds`
- `/admin_chatbots`

Ключевые admin-операции:
- Операции с parcel:
  - create parcel (root и world-контексты)
  - set owner
  - set geometry
  - set writers/editors
  - delete parcel
  - regenerate parcel screenshots
  - bulk owner/editor update для world parcels
  - bulk world parcel delete (с защитами)
- Feature flags и server controls:
  - read-only mode toggle
  - server admin message
  - server script execution flag
  - Lua HTTP flag
  - world maintenance flag
  - chatbots run flag
  - принудительный запуск dynamic texture update checker
  - reload web data
- Администрирование пользователей:
  - просмотр user details
  - set world gardener flag
  - set dynamic texture update permission
  - admin reset user password
- Администрирование чатботов:
  - фильтрация чатботов по миру/пользователю
  - enable/disable одного/нескольких
  - delete одного/нескольких
  - bulk update профиля чатботов
- Администрирование карты:
  - regenerate map tiles (mark not done)
  - recreate map tiles
  - просмотр статуса map tiles
- Инструменты модерации transactions/orders/auctions
- Admin audit log

---

## 13. Что должно стать wiki-страницами (предлагаемый порядок)

1. Установка (Windows)
2. Регистрация и вход
3. Первый запуск и подключение к миру
4. Перемещение и режимы камеры
5. Обзор UI (меню, toolbar, docks)
6. Добавление контента: model/image/video/webview/audio/voxels/text/hypercard/decal/portal
7. Полный гайд по Object Editor
8. Гайд по материалам и текстурированию
9. Базовый гайд по lightmaps и оптимизации
10. Parcel-система и права доступа
11. Personal world и создание/редактирование world
12. Photo mode и social upload
13. Транспорт и жесты
14. Гайд по Camera + Camera Screen streaming
15. Web-возможности аккаунта (chatbots/events/news/photos)
16. Полный гайд по операциям admin-панели
17. Troubleshooting и частые ошибки
18. Чеклист релиза/обновления

---

## 14. Открытый чеклист проверки перед публикацией финальной wiki

- Проверить flow установки на актуальном релизном пакете (цель: `0.0.11`).
- Повторно проверить все labels меню на текущем билде (RU/EN).
- Повторно проверить каждое Add-действие live-тестом в одном parcel.
- Повторно проверить права create/edit для personal world на сервере.
- Повторно проверить admin-действия под superadmin и non-superadmin аккаунтами.
- Добавить скриншоты для каждого ключевого сценария/страницы.
- Для каждой темы сделать краткую (quick start) и расширенную (advanced) версию.

---

Конец черновика.
