# Lua Scripting API в Substrata

## Обзор

Substrata использует язык программирования **Luau** (версия Lua от Roblox) для создания скриптов объектов. Скрипты выполняются как на клиенте, так и на сервере.

## Начало работы

### Создание скрипта

1. Скрипт должен начинаться с `--lua` для указания, что это Luau скрипт
2. Скрипты можно редактировать только для объектов, которые вы создали
3. Скрипт применяется при закрытии редактора скриптов

### Пример минимального скрипта

```lua
--lua

function onUserTouchedObject(av : Avatar, ob : Object)
    showMessageToUser("Привет " .. av.name .. "!", av)
end
```

## Глобальные переменные

- **`this_object : Object`** - Объект, на котором выполняется скрипт
- **`IS_CLIENT : boolean`** - `true` если скрипт выполняется на клиенте
- **`IS_SERVER : boolean`** - `true` если скрипт выполняется на сервере

## Глобальные функции

### Работа с объектами

#### `getObjectForUID(uid : number) : Object`
Получить объект по его UID (уникальному идентификатору).

```lua
local ob = getObjectForUID(5290)
print("Объект: " .. tostring(ob.uid))
```

### Работа со временем

#### `getCurrentTime() : number`
Получить текущее глобальное время в метавселенной (в секундах).

```lua
local start_time = getCurrentTime()
-- ... выполнение действий ...
local elapsed = getCurrentTime() - start_time
print("Прошло времени: " .. elapsed .. " секунд")
```

### Сообщения пользователю

#### `showMessageToUser(msg : string, av : Avatar)`
Показать сообщение пользователю на экране (отображается несколько секунд).

```lua
function onUserTouchedObject(av : Avatar, ob : Object)
    showMessageToUser("Вы коснулись объекта!", av)
end
```

### Таймеры

#### `createTimer(onTimerEvent : function, interval_time_s : number, repeating : bool) : number`
Создать таймер. Обработчик должен иметь сигнатуру: `onTimerEvent(object : Object)`.

**Ограничение:** максимум 4 таймера одновременно.

```lua
local timer_handle = createTimer(function(ob)
    print("Таймер сработал!")
end, 5.0, true) -- Повторяющийся таймер каждые 5 секунд
```

#### `destroyTimer(timer_handle : number)`
Уничтожить таймер.

```lua
destroyTimer(timer_handle)
```

### Обработка событий

#### `addEventListener(event_name : string, ob_uid : number, handler : Function) : number`
Добавить обработчик события для другого объекта.

**Доступные события:**
- `"onUserUsedObject"`
- `"onUserTouchedObject"`
- `"onUserMovedNearToObject"`
- `"onUserMovedAwayFromObject"`
- `"onUserEnteredParcel"`
- `"onUserExitedParcel"`
- `"onUserEnteredVehicle"`
- `"onUserExitedVehicle"`

```lua
-- Слушать событие касания объекта с UID 583
addEventListener("onUserTouchedObject", 583, function(av, ob)
    showMessageToUser("Кто-то коснулся объекта 583!", av)
end)
```

### HTTP запросы

#### `doHTTPGetRequestAsync(URL : string, additional_header_lines : table, onDone : function, onError : function)`
Отправить асинхронный HTTP GET запрос.

**Ограничение:** максимум 5 запросов за 300 секунд на пользователя.

```lua
function onDone(result)
    print("Код ответа: " .. tostring(result.response_code))
    print("Сообщение: " .. result.response_message)
    print("MIME тип: " .. result.mime_type)
    print("Тело: " .. result.body_data)
end

function onError(result)
    print("Ошибка: " .. result.error_description)
end

doHTTPGetRequestAsync(
    "https://api.example.com/data",
    { Authorization = "Bearer token123" },
    onDone,
    onError
)
```

#### `doHTTPPostRequestAsync(URL : string, post_content : string, content_type : string, additional_header_lines : table, onDone : function, onError : function)`
Отправить асинхронный HTTP POST запрос.

```lua
doHTTPPostRequestAsync(
    "https://api.example.com/data",
    '{"id": "123"}',
    "application/json",
    { Authorization = "Bearer token123" },
    onDone,
    onError
)
```

### Работа с секретами

#### `getSecret(secret_name : string) : string`
Получить секретное значение, установленное через страницу Account Secrets (`/secrets`).

```lua
local api_key = getSecret("MY_API_KEY")
```

### Работа с JSON

#### `parseJSON(json : string) : table`
Распарсить JSON строку в Lua таблицу.

```lua
local data = parseJSON('{"name": "test", "value": 123}')
print(data.name) -- "test"
print(data.value) -- 123
```

### Хранилище объектов

#### `objectstorage.setItem(key : string, value : Any)`
Сохранить значение в постоянное хранилище объекта. Значения сохраняются между перезапусками сервера и изменениями скрипта.

```lua
objectstorage.setItem("visitor_count", 42)
```

#### `objectstorage.getItem(key : string) : Any`
Получить значение из хранилища объекта. Возвращает `nil` если значение не найдено.

```lua
local count = objectstorage.getItem("visitor_count")
if count == nil then
    count = 0
end
count = count + 1
objectstorage.setItem("visitor_count", count)
```

## События (Event Handlers)

Эти функции автоматически вызываются при соответствующих событиях, если они определены в скрипте.

### `onUserTouchedObject(avatar : Avatar, object : Object)`
Вызывается когда аватар касается объекта. Если аватар продолжает касаться объекта (например, стоит на нём), событие продолжает срабатывать примерно каждые 0.5 секунды.

```lua
function onUserTouchedObject(av : Avatar, ob : Object)
    showMessageToUser("Привет, " .. av.name .. "!", av)
end
```

### `onUserUsedObject(avatar : Avatar, object : Object)`
Вызывается когда пользователь использует объект (наводит курсор и нажимает E).

```lua
function onUserUsedObject(av : Avatar, ob : Object)
    -- Телепортировать пользователя
    local p = av.pos
    p.x = 100
    p.y = 50
    p.z = 10
    av.pos = p
end
```

### `onUserMovedNearToObject(avatar : Avatar, object : Object)`
Вызывается когда аватар приближается к объекту (в пределах 20 метров).

```lua
function onUserMovedNearToObject(av : Avatar, ob : Object)
    print(av.name .. " приблизился к объекту")
end
```

### `onUserMovedAwayFromObject(avatar : Avatar, object : Object)`
Вызывается когда аватар отдаляется от объекта (дальше 20 метров).

### `onUserEnteredParcel(avatar : Avatar, object : Object, parcel : Parcel)`
Вызывается когда аватар входит в участок земли, где находится объект скрипта.

### `onUserExitedParcel(avatar : Avatar, object : Object, parcel : Parcel)`
Вызывается когда аватар покидает участок земли.

### `onUserEnteredVehicle(avatar : Avatar, vehicle_ob : Object)`
Вызывается когда аватар садится в транспортное средство.

### `onUserExitedVehicle(avatar : Avatar, vehicle_ob : Object)`
Вызывается когда аватар выходит из транспортного средства.

## Классы и их свойства

### Object (Объект)

#### Свойства трансформации
- **`pos : Vec3d`** - Позиция объекта в мире
- **`axis : Vec3f`** - Ось вращения
- **`angle : number`** - Угол вращения вокруг оси (в радианах, против часовой стрелки)
- **`scale : Vec3f`** - Масштаб по осям X, Y, Z

#### Физические свойства
- **`collidable : boolean`** - Может ли объект сталкиваться с другими объектами
- **`dynamic : boolean`** - Является ли объект динамическим физическим объектом
- **`sensor : boolean`** - Сенсорный объект (генерирует события касания, но объекты проходят сквозь него)
- **`mass : number`** - Масса объекта в кг (только для dynamic объектов)
- **`friction : number`** - Коэффициент трения (обычно 0-1, 0 = нет трения, 1 = максимальное трение)
- **`restitution : number`** - Коэффициент упругости (0-1, 0 = неупругий удар, 1 = упругий удар)
- **`centre_of_mass_offset_os : Vec3f`** - Смещение центра масс в локальных координатах

#### Модель и материалы
- **`model_url : string`** - URL 3D модели объекта
- **`materials : table`** - Таблица материалов объекта
- **`getNumMaterials() : number`** - Получить количество материалов
- **`getMaterial(mat_index : number) : Material`** - Получить материал по индексу

#### Контент
- **`content : string`** - Текстовое содержимое (для Text и Hypercard объектов)
- **`target_url : string`** - Целевой URL (для порталов и ссылок)

#### Видео
- **`video_autoplay : boolean`** - Автоматическое воспроизведение видео
- **`video_loop : boolean`** - Зацикливание видео
- **`video_muted : boolean`** - Беззвучное воспроизведение

#### Аудио
- **`audio_source_url : string`** - URL аудио файла (mp3 или wav)
- **`audio_volume : number`** - Громкость аудио (по умолчанию 1.0)
- **`audio_loop : boolean`** - Зацикливание аудио
- **`playAudio()`** - Начать/возобновить воспроизведение аудио
- **`isPlayingAudio() : boolean`** - Проверить, воспроизводится ли аудио

#### Скрипт
- **`script : string`** - Текст скрипта объекта (только для чтения)

### Material (Материал)

#### Цвет и текстуры
- **`colour : Vec3f`** - Цвет отражения (sRGB, Vec3f(1,1,1) = белый)
- **`colour_texture_url : string`** - URL текстуры цвета
- **`emission_rgb : Vec3f`** - Цвет свечения (sRGB)
- **`emission_texture_url : string`** - URL текстуры свечения
- **`normal_map_url : string`** - URL карты нормалей

#### Физические свойства материала
- **`roughness_val : number`** - Шероховатость (0 = гладкий, 1 = шероховатый)
- **`roughness_texture_url : string`** - URL текстуры шероховатости-металличности
- **`metallic_fraction_val : number`** - Металличность (0 = не металл, 1 = металл)
- **`opacity_val : number`** - Непрозрачность (1 = непрозрачный, < 1 = прозрачный)

#### Дополнительные свойства
- **`tex_matrix : Matrix2f`** - Матрица текстурных координат (по умолчанию {1, 0, 0, 1})
- **`emission_lum_flux_or_lum : number`** - Яркость свечения (для прожекторов - световой поток, для моделей - яркость)
- **`hologram : boolean`** - Является ли материал голограммой (не блокирует свет, только излучает)
- **`double_sided : boolean`** - Рендерить обратную сторону треугольников

### Avatar (Аватар)

- **`pos : Vec3d`** - Позиция аватара (примерно на уровне глаз, 1.67 м над поверхностью)
- **`name : string`** - Имя пользователя, управляющего аватаром
- **`linear_velocity : Vec3f`** - Линейная скорость аватара (м/с)
- **`vehicle_inside : Object`** - Транспортное средство, в котором находится аватар (или `nil`)

## Примеры скриптов

### Простой счетчик посетителей

```lua
--lua

local visit_count = objectstorage.getItem("visit_count")
if visit_count == nil then
    visit_count = 0
end

function onUserTouchedObject(av : Avatar, ob : Object)
    visit_count = visit_count + 1
    objectstorage.setItem("visit_count", visit_count)
    showMessageToUser("Посетителей: " .. visit_count, av)
end
```

### Телепорт

```lua
--lua

function onUserTouchedObject(av : Avatar, ob : Object)
    local p = av.pos
    p.x = -1840.3
    p.y = -0.7
    p.z = 1.69
    av.pos = p
    showMessageToUser("Телепортировано!", av)
end
```

### Таймер с изменением цвета

```lua
--lua

local timer_handle = nil
local color_index = 0
local colors = {
    {1, 0, 0}, -- Красный
    {0, 1, 0}, -- Зеленый
    {0, 0, 1}, -- Синий
}

function onUserTouchedObject(av : Avatar, ob : Object)
    if timer_handle == nil then
        timer_handle = createTimer(function(ob)
            local mat = ob.getMaterial(0)
            if mat then
                color_index = (color_index + 1) % #colors
                mat.colour = Vec3f(colors[color_index + 1][1], colors[color_index + 1][2], colors[color_index + 1][3])
            end
        end, 1.0, true) -- Менять цвет каждую секунду
    else
        destroyTimer(timer_handle)
        timer_handle = nil
    end
end
```

### Система гонок с чекпоинтами

```lua
--lua

local race_info = {} -- Состояние гонки для каждого аватара

local waypoint_uids = {
    5290, -- START
    5148, -- CP1
    5156, -- CP2
    5157, -- CP3
    5159, -- CP4
    5160, -- CP5
    5161, -- FINISH
}

local reset_ob_uid = 5588

function onUserTouchedObject(av : Avatar, ob : Object)
    local st = race_info[av.uid]
    if not st then
        st = { next_waypoint_index = 1, valid = true }
        race_info[av.uid] = st
    end

    -- RESET
    if ob.uid == reset_ob_uid then
        showMessageToUser("Подготовка к старту - коснитесь 5290!", av)
        st.next_waypoint_index = 1
        st.valid = true
        return
    end

    if not st.valid then
        showMessageToUser("Неверно. Коснитесь RESET (5588).", av)
        return
    end

    -- Правильный чекпоинт?
    if ob.uid == waypoint_uids[st.next_waypoint_index] then
        if st.next_waypoint_index == 1 then
            -- START
            local veh = av.vehicle_inside
            if not veh then
                showMessageToUser("Вы должны быть в транспорте!", av)
                st.valid = false
                return
            end
            showMessageToUser("Гонка началась!", av)
            st.vehicle = veh
            st.start_time = getCurrentTime()
            st.next_waypoint_index = 2
        elseif st.next_waypoint_index == #waypoint_uids then
            -- FINISH
            local t = getCurrentTime() - st.start_time
            showMessageToUser("Финиш! Ваше время: " .. string.format("%.3f", t) .. " с", av)
            st.valid = false
        else
            -- MID CHECKPOINT
            st.next_waypoint_index = st.next_waypoint_index + 1
            showMessageToUser("Чекпоинт пройден!", av)
        end
    else
        -- НЕВЕРНЫЙ чекпоинт
        showMessageToUser("Неверный чекпоинт!", av)
        st.valid = false
    end
end
```

### HTTP запрос к API

```lua
--lua

function onUserTouchedObject(av : Avatar, ob : Object)
    local api_key = getSecret("WEATHER_API_KEY")
    
    doHTTPGetRequestAsync(
        "https://api.weather.com/data?key=" .. api_key,
        {},
        function(result)
            if result.response_code == 200 then
                local data = parseJSON(result.body_data)
                showMessageToUser("Температура: " .. tostring(data.temperature) .. "°C", av)
            else
                showMessageToUser("Ошибка API: " .. result.response_message, av)
            end
        end,
        function(error)
            showMessageToUser("Ошибка: " .. error.error_description, av)
        end
    )
end
```

## Отладка

### Вывод в консоль

Используйте функцию `print()` для вывода отладочной информации:

```lua
print("Отладочное сообщение")
print("Значение переменной: " .. tostring(my_var))
```

### Просмотр логов

- **Клиент:** Tools > Show Log
- **Сервер:** https://substrata.info/script_log (требуется авторизация)

### Обработка ошибок

При ошибке в скрипте:
1. Ошибка отображается в логе
2. Скрипт останавливается
3. Для перезапуска нужно отредактировать скрипт

## Важные замечания

1. **Скрипты выполняются на клиенте И сервере** - некоторые операции работают только на одной стороне
2. **Физика вычисляется на клиенте** - изменения физических свойств работают клиентски
3. **Другие изменения синхронизируются через сервер** - изменения отправляются на сервер и синхронизируются с клиентами
4. **Ограничение таймеров:** максимум 4 таймера одновременно
5. **Ограничение HTTP:** максимум 5 запросов за 300 секунд на пользователя
6. **Хранилище объектов:** значения сохраняются между перезапусками сервера

## Типы данных

### Vec3d / Vec3f
Вектор из 3 компонентов (x, y, z). Vec3d использует double, Vec3f использует float.

```lua
local pos = Vec3d(10.5, 20.3, 1.67)
print(pos.x) -- 10.5
print(pos.y) -- 20.3
print(pos.z) -- 1.67
```

### Matrix2f
Матрица 2x2 для текстурных координат.

```lua
local tex_matrix = Matrix2f(1, 0, 0, 1)
```

## Дополнительные ресурсы

- [Luau документация](https://luau-lang.org/)
- [Lua документация](https://www.lua.org/)
- [Substrata документация по скриптингу](https://substrata.info/about_luau_scripting)



