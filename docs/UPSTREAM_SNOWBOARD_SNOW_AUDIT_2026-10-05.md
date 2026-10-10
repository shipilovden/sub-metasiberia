# Сноуборд, снег, загрузка и рендеринг: аудит upstream

Дата: 2026-10-05. Статус: аудит дополнен реализацией и сборкой 2026-10-06.

## Обновление реализации 2026-10-06

Перенесены и проверены сборкой: защита вызова транспорта с другим водителем для всех шести типов, появление доски на 2 м, `jump_speed = 3`, белые снежные частицы вместо коричневого дыма, раннее создание локального аватара, пакетная GPU-загрузка ресурсов, поддержка MATL-материалов `.vox` и перенос их свойств в материалы мира. Кнопки и исправления позы сноубордиста остаются в рабочем дереве.

Не переносились крупные связанные группы upstream для GPU-резидентных данных объектов и улучшений экранных отражений (`403a5663`, `c1c0e052`, `e053e209`, `2d8112bd`): они затрагивают текущие локальные изменения `glare-core/opengl/OpenGLEngine.cpp` и требуют отдельной интеграции и визуальной проверки.

## Источники и границы

Выполнены `git fetch upstream master` для Substrata и `git fetch origin master` для glare-core. Проверенные вершины:

- [glaretechnologies/substrata](https://github.com/glaretechnologies/substrata): `c3fc063e093e4f20afa783f3cd4f233fe999132d`.
- [glaretechnologies/glare-core](https://github.com/glaretechnologies/glare-core): `46d4025959cb461e54edf405b93f5241219b0921`.
- Локальная Metasiberia: `fcd0c407` плюс существующие незакоммиченные изменения сноуборда.
- Локальная библиотека `C:\programming\glare-core`: `1e3e42ec` плюс существующие изменения `OpenGLEngine.cpp` и `WMFVideoReader.cpp`.

Состояние «есть» ниже относится к рабочим исходникам, включая незакоммиченные файлы. Оно не доказывает работоспособность в игре или наличие в конкретном запущенном клиенте. Рабочие файлы приложения и библиотек не изменялись; обновлены только удалённые Git-ссылки и добавлен этот отчёт. Сборки, тесты и deployment в этом исследовании не выполнялись.

## 1. Сноуборд: указанные коммиты и фактическое состояние

Все восемь указанных пользователем SHA найдены в истории Substrata.

| Изменение | Коммит Substrata | Состояние в Metasiberia |
| --- | --- | --- |
| Исходная физика и XML-настройки | `e77ed06c` | Добавлены в рабочее дерево; контроллер адаптирован к нашей версии Jolt/PhysicsObject. |
| Вызов доски, ресурсы, UI | `73f2e957` | Добавлены; верхнее меню и нижняя панель имеют собственную локализацию и раскладку. |
| Туловище, ноги, руки, автоматическое выпрямление | `7c3cce0b` | Частично: пружинная модель, IK ног и выпрямление есть; применение новых вращений рук и груди перенесено не полностью. |
| Заряд прыжка удержанием пробела; прыжок при отпускании | `072e8d21` | Есть в `SnowboardPhysics::update()`, полная зарядка за 0,6 с. |
| Брызги при поворотах/боковом скольжении | `072e8d21` | Есть `emitCarveParticles()`. В текущем upstream и у нас это коричневые частицы пыли; подробности ниже. |
| Не перемещать вызовом транспорт с другим водителем | `072e8d21` | Отсутствует во всех шести функциях вызова: bike, hovercar, boat, jet ski, snowboard, car. |
| Смена направления доски при движении задним концом вперёд | `cb53825a` | Есть `forward_direction`, переключение по продольной скорости с порогом ±0,5 м/с. |
| Сопротивление воздуха и трение | `d378f0ab` | Есть в контроллере; коэффициент трения задан как для снега независимо от материала поверхности. |
| Временные слои TERRAIN/MOVING_NO_TERRAIN и битовая матрица столкновений | `3628d3eb` | Битовая матрица не перенесена. У нас свой набор слоёв, включая VEHICLES, и фильтр со `switch`. |
| Отказ от временных слоёв, приподнятый collision box | `a14de940` | Финальная геометрия столкновений есть: `riding_shape` / `raised_riding_shape`. Временные слои восстанавливать не нужно. |
| Появление доски ближе: 2 м вместо 3 м | `55d7928a` | Отсутствует: наша `summonSnowboard()` использует множитель `3.0`. |
| Ранняя загрузка своего аватара | `35c6e1ba` | Не перенесено; см. раздел 4. |

### Неполный перенос анимации `7c3cce0b`

- [SnowboardPhysics.cpp](../gui_client/SnowboardPhysics.cpp): `updateTorsoSpring()` вычисляет движение тела и рук, `updateRiderPose()` заполняет `snowboard_upper_arm_rot_ss` и `snowboard_forearm_rot_ss`.
- [AvatarGraphics.h](../gui_client/AvatarGraphics.h): поля этих вращений присутствуют.
- [AvatarGraphics.cpp](../gui_client/AvatarGraphics.cpp): вращения плеч и предплечий не читаются. В upstream добавлен вызов `rotateRiderBoneAroundPivot()` для пяти костей: `Spine2`, двух плеч и двух предплечий. У нас этот блок отсутствует.
- Вращение `snowboard_chest_rot_ss` у нас применяется к `hips_node_i`; upstream применяет его к `spine2_node_i` вокруг положения кости и сбрасывает эту процедурную трансформацию при следующем обновлении.
- [Scripting.cpp](../gui_client/Scripting.cpp): значение `SnowboardScriptSettings::jump_speed` по умолчанию осталось `4.f`, upstream снизил его до `3.f`. В [summoned_snowboard_script.xml](../resources/summoned_snowboard_script.xml) уже указано `3`, поэтому несоответствие относится к скриптам без явного `jump_speed`.

### Адаптации физики, которые нужно сохранить

Сравнение `SnowboardPhysics.cpp/.h` с актуальным upstream показывает небольшие адаптации API: сохранённый `vehicle_collision_tester`, локальный флаг `tilt_limit_engaged`, множитель сглаживания `3.f` вместо новой константы, `physics_ob.dynamic` вместо `motion_type`. Основная физика соответствует текущему upstream. Не заменять эти файлы вслепую: наша версия Jolt и PhysicsObject отличается.

## 2. Где находятся модель, материалы и связанная логика

| Область | Файлы |
| --- | --- |
| Контроллер доски, прыжки, устойчивость, сопротивление, частицы | [SnowboardPhysics.cpp](../gui_client/SnowboardPhysics.cpp), [SnowboardPhysics.h](../gui_client/SnowboardPhysics.h) |
| Поза аватара и IK | [AvatarGraphics.cpp](../gui_client/AvatarGraphics.cpp), [AvatarGraphics.h](../gui_client/AvatarGraphics.h), [VehiclePhysics.h](../gui_client/VehiclePhysics.h) |
| Вызов и создание контроллера | [GUIClient.cpp](../gui_client/GUIClient.cpp), `summonSnowboard()` и `createVehicleControllerForScript()` |
| XML-парсер и настройки | [Scripting.cpp](../gui_client/Scripting.cpp), [Scripting.h](../gui_client/Scripting.h), [summoned_snowboard_script.xml](../resources/summoned_snowboard_script.xml) |
| Кнопки вызова | [GestureUI.cpp](../gui_client/GestureUI.cpp), [MainWindow.ui](../gui_client/MainWindow.ui), [MainWindow.cpp](../gui_client/MainWindow.cpp), [RuntimeTranslation.cpp](../gui_client/RuntimeTranslation.cpp) |
| Идентификатор модели | [VehiclesShared.h](../shared/VehiclesShared.h) |
| Материалы модели | [snowboard_mats.xml](../resources/snowboard_mats.xml) |
| Серверное разрешение вызова в нашей версии | [WorkerThread.cpp](../server/WorkerThread.cpp), allowlist моделей |
| Удаление неиспользуемых вызванных досок в нашем рабочем дереве | [WorldMaintenance.cpp](../server/WorldMaintenance.cpp) |
| Upstream-разрешения и подготовка LOD | `server/ObjectPermissions.cpp`, `server/MeshLODGenThread.cpp` в upstream |

Модель: `_UB_2026_Black_Red_Snowboard_glb_13786249306117967103.bmesh`.

Текстуры из `snowboard_mats.xml`:

- Цвет: `GLB_image_16953310986919210825_jpg_16953310986919210825.jpg`.
- Нормали: `GLB_image_9161215156477569574_jpg_9161215156477569574.jpg`.
- Карта шероховатости: `GLB_image_3368751828563657912_jpg_3368751828563657912.jpg`.

XML материалов и XML скрипта совпадают с upstream после нормализации переводов строк. Сам `.bmesh` не входит в дерево файлов upstream Git: это ресурс мира, на который ссылается код. Наличие бинарного ресурса в серверном хранилище или клиентском кэше в этом аудите не проверялось. В upstream доска также включена в явный список генерации оптимизированных моделей; в нашей версии `MeshLODGenThread.cpp` такого явного упоминания доски нет, общий механизм LOD требует отдельного сопоставления.

## 3. Что именно найдено про снег

### 3.1 Брызги от сноуборда

`SnowboardPhysics::emitCarveParticles()` есть и в upstream, и у нас. Интенсивность зависит от скорости бокового скольжения и наклона доски; частицы вылетают из нижнего канта.

Но фактические параметры: `ParticleType_Smoke`, цвет `Colour3f(0.62f, 0.54f, 0.44f)`, комментарий `Light dirt`. Рендерер [ParticleManager.cpp](../gui_client/ParticleManager.cpp) использует набор `resources/sprites/smoke_sprite_*.basis`. Поэтому формулировка «снежные брызги» в описании обновления шире текущей реализации: белый снег и определение типа поверхности здесь не реализованы.

`SnowboardPhysics::update()` использует фиксированный `friction_mu = 0.05`, с комментарием «assume snow for now». В этом контроллере нет выбора трения и цвета брызг по материалу земли. Накопление снега, прорезание колеи и деформация снежного слоя в исследованной реализации не найдены.

### 3.2 Старое снежное покрытие в glare-core

- [d76dbf47](https://github.com/glaretechnologies/glare-core/commit/d76dbf47), 2023-12-23: временный эффект снега, в одном коммите с северным сиянием.
- [a688b661](https://github.com/glaretechnologies/glare-core/commit/a688b661), 2024-01-23: эффект снега отключён.
- В текущем `origin/master` код по-прежнему закомментирован: `opengl/shaders/phong_frag_shader.glsl`, `opengl/OpenGLEngine.cpp/.h`.
- Схема эффекта: маска по направлению нормали `smoothstep(0.56, 0.6, normal.z)`, подавление под водой и на decal, белый albedo, снижение metallic, шероховатость к `0.6`.
- Предполагалась текстура `snow-ice-01-normal.png` через `setSnowIceTexture()`. Вызов в `Substrata/gui_client/GlWidget.cpp` также закомментирован. Текстура с этим именем в проверенных Git-деревьях не найдена.

Это исторический визуальный эффект покрытия поверхностей. Для включения сейчас нужно отдельно адаптировать его к текущему рендереру и подготовить ресурс текстуры; одного переноса сноуборда недостаточно.

### 3.3 Снегопад, уже существующий в Metasiberia

В нашем коде есть отдельный пресет частиц `snow`, не часть указанной серии upstream-коммитов:

- [ParticleEmitterSettings.cpp](../gui_client/ParticleEmitterSettings.cpp): пресет около строки 629; `ParticleKind_Snowflake`, `builtin:snowflake`, эмиттер Box, направление вниз, ветер, турбулентность, светлый цвет, исчезновение при ударе о поверхность.
- [GUIClient.cpp](../gui_client/GUIClient.cpp): обработка `builtin:snowflake` и генерация встроенного изображения частицы.
- [ObjectEditor.cpp](../gui_client/ObjectEditor.cpp): пункт `Snow` в списке пресетов.
- [RuntimeTranslation.cpp](../gui_client/RuntimeTranslation.cpp): перевод `Snow` → `Снег`.

Наличие этого пресета не означает снежное покрытие terrain или сцепление доски с отдельным материалом снега. Его поведение в запущенном клиенте в этом аудите не проверялось.

## 4. Ранняя загрузка аватара

[35c6e1ba](https://github.com/glaretechnologies/substrata/commit/35c6e1ba) меняет `GUIClient.cpp/.h` и `ClientThread.cpp`:

- Автологин отправляется из `connectToServer()` до `QueryObjectsInAABB`.
- Добавлены `getAutoLoginUsername()` и `createOurAvatarLocally()`.
- При `LoggedInMessage` свой аватар сразу создаётся или обновляется локально; для гостя создаётся без ожидания серверного `AvatarCreated`.
- Последующее `AvatarCreated` не заменяет уже созданного локального аватара устаревшим состоянием.

У нас этих помощников нет; автологин остаётся в обработке сообщения о подключении. При переносе необходимо сохранить собственный выбор гостевого аватара (`guest_avatar_selection_pending/ready`), экипировку и локальные настройки жестов. Прямое применение большого patch без адаптации может нарушить эти сценарии.

## 5. Рендеринг, загрузка и отражения

Пункты без SHA из пользовательского списка сопоставлены с коммитами по их сообщениям и diff. Это найденные технические изменения, а не подтверждённые измерения FPS или времени загрузки на компьютере пользователя.

| Эффект | glare-core | Связанные изменения Substrata / файлы |
| --- | --- | --- |
| Данные объектов на GPU даже без полного bindless-пути; меньше повторных привязок материалов | `403a5663` | `97dbc754` для `shaders/portal_vert_shader.glsl`; `OpenGLEngine.cpp/.h`, `OpenGLProgram.cpp/.h`, группа vertex shaders. В описании особенно отмечены ускорения на AMD в некоторых сценах. |
| Пакетная загрузка моделей и текстур на GPU вместо ожидания после каждого ресурса | `89f25b0e` | `c0c7b9da`: переход на `enqueueUpload()` в `LoadModelTask`, `LoadTextureTask`, `AnimatedTextureManager`, `MakeHypercardTextureTask`. |
| Обновление облачной environment map полосами | `86bcd03b` | `OpenGLEngine.cpp/.h`. |
| Ускорение SSAO и его размытия | `98d2a430`, `7bc20a77`, `f2e17d4d`, `6db381a2`, `4b2ecd94`, `826d789b` | `compute_ssao_frag_shader.glsl`, `blur_ssao_frag_shader.glsl`, uniform-структуры, `OpenGLEngine`, `FrameBuffer`; включая packed depth+normal texture и ранние выходы. |
| Улучшение отражений небольших ярких источников и уменьшение мерцания | `c1c0e052` | `OpenGLEngine.cpp/.h`, `phong_frag_shader.glsl`. |
| Размытие зеркальных отражений с учётом вклада соседних пикселей | `e053e209` | `blur_ssao_frag_shader.glsl`, подход scatter-as-gather. |
| Шейдер resolve для предварительного прохода | `2d8112bd` | Новый `opengl/shaders/prepass_resolve_frag_shader.glsl`; включать вместе с соответствующим кодом рендерера. |
| Оптимизация трассировки преломления воды | `aee06c9d` | `water_frag_shader.glsl`. |
| Дополнительные свежие оптимизации: раннее отбрасывание травы, упрощение глубоко подводного дна | `8a39507e`, `9526ec8b` | `imposter_frag_shader.glsl`, `phong_frag_shader.glsl`, общие uniforms и `OpenGLEngine`. |

Основные новые изменения (`403a5663`, `89f25b0e`, `c1c0e052`, `e053e209`, `826d789b`, `2d8112bd`) отсутствуют в истории локальной библиотеки; проверка кода также не нашла ключевые новые API и shader-макросы. В локальных заданиях загрузки ещё используется `getMessageQueue().enqueue()` вместо нового `enqueueUpload()`. При этом у нашего форка уже есть собственные правки облаков, воды и анимации: нельзя считать любые оптимизации отсутствующими только по несовпадению SHA.

Канонический `C:\programming\qt_build.ps1` указывает `GLARE_CORE_TRUNK_DIR = C:/programming/glare-core` и копирует runtime-шейдеры из `glare-core/opengl/shaders`. Перенос только C++ без соответствующих GLSL и runtime-ресурсов не образует согласованный комплект. В этом аудите библиотека и её рабочие шейдеры не обновлялись.

## 6. Материалы MagicaVoxel `.vox`

Нужна пара изменений в двух репозиториях:

1. [glare-core `27004b9b`](https://github.com/glaretechnologies/glare-core/commit/27004b9b): `graphics/FormatDecoderVox.cpp/.h` читает чанки `MATL`, типы материалов и их свойства; добавлен `testfiles/vox/emission.vox`.
2. [Substrata `fb58d292`](https://github.com/glaretechnologies/substrata/commit/fb58d292): `gui_client/ModelLoading.cpp` переносит roughness, glass/opacity, metal и emission/flux в материалы мира.

Оба изменения отсутствуют в исследованных локальных реализациях. Сейчас наша ветка загрузки `.vox` берёт цвет палитры без новой обработки свойств материалов.

## 7. Порядок последующего переноса

1. Завершить уже начатый сноуборд: применение вращений костей, default `jump_speed = 3`, защита вызова всех шести видов транспорта, появление доски на расстоянии 2 м. Сохранить местную UI-раскладку и адаптации Jolt.
2. Отдельно адаптировать битовую матрицу к нашим физическим слоям. Не добавлять уже отменённые TERRAIN/MOVING_NO_TERRAIN и не стирать VEHICLES.
3. Перенести раннюю загрузку аватара с учётом нашего выбора гостя и экипировки.
4. Перенести MATL как согласованную пару декодер + ModelLoading.
5. Переносить загрузку GPU и рендеринг согласованными группами C++/GLSL, сохраняя местные изменения glare-core. Проверять Qt и затронутый общий SDL/Emscripten-путь отдельно.
6. Снежное покрытие и частицы на снегу требуют отдельного решения: сейчас upstream даёт старый отключённый шейдер покрытия и универсальную пыль от канта; у Metasiberia есть собственный снегопад. Готовой единой системы накопления снега в исследованных источниках не найдено.

Перечисленные шаги — план по результатам поиска. Они не выполнены этим аудитом и не означают разрешение на новое обновление зависимостей или production.
