#include "RuntimeTranslation.h"


using namespace RuntimeTranslation;


QString RuntimeTranslator::key(const char* context, const char* sourceText)
{
	return QString::fromLatin1(context) + QChar(0x04) + QString::fromUtf8(sourceText);
}


RuntimeTranslator::RuntimeTranslator(QObject* parent)
:	QTranslator(parent)
{
	auto add = [this](const char* context, const char* src, const char* dst)
	{
		ru_translations.insert(key(context, src), QString::fromUtf8(dst));
	};

	// Main menu
	add("MainWindow", "Edit", "Правка");
	add("MainWindow", "Movement", "Движение");
	add("MainWindow", "Avatar", "Аватар");
	add("MainWindow", "Vehicles", "Транспорт");
	add("MainWindow", "Gear", "Снаряжение");
	add("MainWindow", "View", "Вид");
	add("MainWindow", "Go", "Переход");
	add("MainWindow", "Tools", "Инструменты");
	add("MainWindow", "Window", "Окно");
	add("MainWindow", "Themes", "Темы");
	add("MainWindow", "Help", "Справка");
	add("MainWindow", "Language", "Язык");
	add("MainWindow", "English", "English");
	add("MainWindow", "Русский", "Русский");

	// Window menu and dock widgets
	add("MainWindow", "Reset Layout", "Сбросить раскладку");
	add("MainWindow", "Enter Fullscreen Mode", "Полноэкранный режим");
	add("MainWindow", "Editor", "Редактор");
	add("MainWindow", "Material Browser", "Браузер материалов");
	add("MainWindow", "Environment", "Окружение");
	add("MainWindow", "World Settings", "Настройки мира");
	add("MainWindow", "Webcam", "Веб-камера");
	add("MainWindow", "Chat", "Чат");
	add("MainWindow", "Help Information", "Справочная информация");
	add("MainWindow", "Diagnostics", "Диагностика");
	add("MainWindow", "Lightmaps", "Лайтмапы");
	add("MainWindow", "Bake Lightmaps (fast) for all objects in parcel", "Запечь лайтмапы (быстро) для всех объектов участка");
	add("MainWindow", "Bake lightmaps (high quality) for all objects in parcel", "Запечь лайтмапы (высокое качество) для всех объектов участка");

	// Toolbar / actions
	add("MainWindow", "Add Model / Image", "Добавить модель / изображение");
	add("MainWindow", "Add Video", "Добавить видео");
	add("MainWindow", "Add Hypercard", "Добавить гиперкарточку");
	add("MainWindow", "Add Voxels", "Добавить воксели");
	add("MainWindow", "Add Web View", "Добавить Web View");
	add("MainWindow", "Parcels", "Участки");
	add("MainWindow", "Avatar Settings", "Настройки аватара");
	add("MainWindow", "About Metasiberia", "О Metasiberia");
	add("MainWindow", "Add Text", "Добавить текст");
	add("MainWindow", "Add Spotlight", "Добавить прожектор");
	add("MainWindow", "Add Camera", "Добавить камеру");
	add("MainWindow", "Add Seat", "Добавить сиденье");
	add("MainWindow", "Add Audio Player", "Добавить аудио-плеер");
	add("MainWindow", "Add Decal", "Добавить декаль");
	add("MainWindow", "Add Portal", "Добавить портал");
	add("MainWindow", "Add to Favorites", "Добавить в избранное");
	add("MainWindow", "Save Object To Disk", "Сохранить объект на диск");
	add("MainWindow", "Save Parcel Objects To Disk", "Сохранить объекты участка на диск");
	add("MainWindow", "Load Object(s) From Disk", "Загрузить объект(ы) с диска");
	add("MainWindow", "Summon Bike", "Вызвать байк");
	add("MainWindow", "Summon Hovercar", "Вызвать ховеркар");
	add("MainWindow", "Summon Boat", "Вызвать лодку");
	add("MainWindow", "Summon Jet Ski", "Вызвать гидроцикл");
	add("MainWindow", "Summon Car", "Вызвать машину");
	add("MainWindow", "Fly Mode     F", "Режим полёта     F");
	add("MainWindow", "Third Person Camera       V", "Камера от третьего лица       V");
	add("MainWindow", "Open Gear Inventory", "Открыть инвентарь снаряжения");
	add("MainWindow", "Convert Selected Object To Gear Item", "Преобразовать выбранный объект в предмет снаряжения");
	add("MainWindow", "Go Back", "Назад");
	add("MainWindow", "Go to Position", "Перейти к позиции");
	add("MainWindow", "Go to Parcel", "Перейти к участку");
	add("MainWindow", "Go To Start Location", "Перейти к стартовой локации");
	add("MainWindow", "Go to Main World", "Перейти в главный мир");
	add("MainWindow", "Go to Personal World", "Перейти в персональный мир");
	add("MainWindow", "Go to Cryptovoxels World", "Перейти в мир Cryptovoxels");
	add("MainWindow", "Go to Substrata", "Перейти в Substrata");
	add("MainWindow", "Go to Metasiberia", "Перейти в Metasiberia");
	add("MainWindow", "Go to Shki-Nvkz", "Перейти в Shki-Nvkz");
	add("MainWindow", "Go to Favorites", "Переход в избранное");
	add("MainWindow", "Set current location as start location", "Сделать текущую локацию стартовой");

	// Frequently used menu actions
	add("MainWindow", "Undo", "Отменить");
	add("MainWindow", "Redo", "Повторить");
	add("MainWindow", "Delete Object", "Удалить объект");
	add("MainWindow", "Clone Object", "Клонировать объект");
	add("MainWindow", "Copy Object", "Копировать объект");
	add("MainWindow", "Paste Object", "Вставить объект");
	add("MainWindow", "Find Object by ID", "Найти объект по ID");
	add("MainWindow", "Find Objects Nearby", "Найти объекты рядом");
	add("MainWindow", "Take Screenshot", "Сделать скриншот");
	add("MainWindow", "Show Screenshot Folder", "Открыть папку скриншотов");
	add("MainWindow", "Show Log", "Показать лог");
	add("MainWindow", "Options", "Настройки");
	add("MainWindow", "Mute Audio", "Отключить звук");

	// Dynamic strings from MainWindow.cpp
	add("MainWindow", "Rename", "Переименовать");
	add("MainWindow", "Rename Favorite", "Переименовать избранное");
	add("MainWindow", "Rename favorite", "Переименовать избранное");
	add("MainWindow", "Enter new name:", "Введите новое имя:");
	add("MainWindow", "New name:", "Новое имя:");
	add("MainWindow", "Favorite renamed.", "Избранное переименовано.");
	add("MainWindow", "Delete", "Удалить");
	add("MainWindow", "Delete Favorite", "Удалить избранное");
	add("MainWindow", "Delete favorite", "Удалить избранное");
	add("MainWindow", "Are you sure you want to delete this favorite?", "Вы уверены, что хотите удалить это избранное?");
	add("MainWindow", "Delete this favorite?", "Удалить это избранное?");
	add("MainWindow", "Favorite deleted.", "Избранное удалено.");
	add("MainWindow", "(No favorites)", "(Нет избранного)");
	add("MainWindow", "Default", "По умолчанию");
	add("MainWindow", "Open Current Location In Browser", "Открыть текущую локацию в браузере");
	add("MainWindow", "Use the W/A/S/D keys and arrow keys to move and look around.\n", "Используйте клавиши W/A/S/D и стрелки для передвижения и обзора.\n");
	add("MainWindow", "Click and drag the mouse on the 3D view to look around.\n", "Нажмите и перетаскивайте мышь в 3D-окне для обзора.\n");
	add("MainWindow", "Space key: jump\n", "Пробел: прыжок\n");
	add("MainWindow", "Double-click an object to select it.", "Дважды щёлкните по объекту, чтобы выбрать его.");
	add("MainWindow", "Enable Webcam", "Включить веб-камеру");
	add("MainWindow", "Waiting for webcam frame...", "Ожидание кадра с веб-камеры...");
	add("MainWindow", "Webcam capture is not supported on this platform/build.", "Захват веб-камеры не поддерживается в этой платформе/сборке.");
	add("MainWindow", "Webcam disabled", "Веб-камера выключена");

	// FileSelectWidget
	add("FileSelectWidget", "Browse", "Обзор");
	add("QPlatformTheme", "OK", "ОК");
	add("QPlatformTheme", "Cancel", "Отмена");
	add("QPlatformTheme", "Apply", "Применить");
	add("QPlatformTheme", "Close", "Закрыть");

	// Webcam UI
	add("WebcamControlPanel", "Enable Webcam", "Включить веб-камеру");
	add("WebcamControlPanel", "Photo", "Фото");
	add("WebcamControlPanel", "Capture a photo", "Сделать фото");
	add("WebcamControlPanel", "Record", "Запись");
	add("WebcamControlPanel", "Start recording video", "Начать запись видео");
	add("WebcamControlPanel", "Stop", "Стоп");
	add("WebcamControlPanel", "Stop recording", "Остановить запись");
	add("WebcamControlPanel", "Open folder with photos and recordings", "Открыть папку с фото и записями");
	add("WebcamControlPanel", "Settings", "Настройки");
	add("WebcamControlPanel", "Resolution, FPS, photo and video settings", "Разрешение, FPS, настройки фото и видео");
	add("WebcamControlPanel", "Inactive", "Неактивно");

	add("WebcamSettingsDialog", "Webcam Settings", "Настройки веб-камеры");
	add("WebcamSettingsDialog", "Preview", "Предпросмотр");
	add("WebcamSettingsDialog", "Resolution:", "Разрешение:");
	add("WebcamSettingsDialog", "Frame rate:", "Частота кадров:");
	add("WebcamSettingsDialog", "Photo", "Фото");
	add("WebcamSettingsDialog", "JPEG quality:", "Качество JPEG:");
	add("WebcamSettingsDialog", "Video", "Видео");
	add("WebcamSettingsDialog", " Mbps", " Мбит/с");
	add("WebcamSettingsDialog", "Bitrate:", "Битрейт:");
	add("WebcamSettingsDialog", " FPS", " FPS");

	add("WebcamVideoView", "Webcam feed will appear here", "Здесь появится изображение с веб-камеры");

	add("WebcamWindow", "Webcam", "Веб-камера");
	add("WebcamWindow", "Webcam Settings", "Настройки веб-камеры");
	add("WebcamWindow", "Preview: -", "Предпросмотр: -");
	add("WebcamWindow", "Preview: %1", "Предпросмотр: %1");
	add("WebcamWindow", "No cameras found", "Камеры не найдены");
	add("WebcamWindow", "No camera available.", "Камера недоступна.");
	add("WebcamWindow", "Camera failed to start", "Не удалось запустить камеру");
	add("WebcamWindow", "Active", "Активно");
	add("WebcamWindow", "Inactive", "Неактивно");
	add("WebcamWindow", "Recording...", "Идёт запись...");
	add("WebcamWindow", "Starting...", "Запуск...");
	add("WebcamWindow", "Saving...", "Сохранение...");
	add("WebcamWindow", "Preparing...", "Подготовка...");
	add("WebcamWindow", "Photo failed to start", "Не удалось начать съёмку фото");
	add("WebcamWindow", "Could not start photo capture.", "Не удалось запустить съёмку фото.");
	add("WebcamWindow", "Photo failed", "Ошибка фото");
	add("WebcamWindow", "Photo could not be saved. No image data.", "Не удалось сохранить фото: нет данных изображения.");
	add("WebcamWindow", "Could not save image to file:\n%1", "Не удалось сохранить изображение в файл:\n%1");
	add("WebcamWindow", "Photo could not be saved: %1", "Не удалось сохранить фото: %1");
	add("WebcamWindow", "Saved: %1", "Сохранено: %1");
	add("WebcamWindow", "Saved to: %1", "Сохранено в: %1");
	add("WebcamWindow", "Recording failed", "Ошибка записи");
	add("WebcamWindow", "Video recording error: %1", "Ошибка записи видео: %1");
	add("WebcamWindow", "Camera error", "Ошибка камеры");
	add("WebcamWindow", "Camera error: %1", "Ошибка камеры: %1");
	add("WebcamWindow", "Unknown error", "Неизвестная ошибка");

	// ObjectEditor
	add("ObjectEditor", "Model", "Модель");
	add("ObjectEditor", "Position x/y/z", "Позиция x/y/z");
	add("ObjectEditor", "Scale x/y/z ", "Масштаб x/y/z");
	add("ObjectEditor", "Link x/y/z scale", "Связать масштаб x/y/z");
	add("ObjectEditor", "Rot z/y/x", "Поворот z/y/x");
	add("ObjectEditor", "Show 3D pos/rot controls", "Показать 3D-контролы поз./поворота");
	add("ObjectEditor", "Snap to grid", "Привязка к сетке");
	add("ObjectEditor", "Grid spacing:", "Шаг сетки:");
	add("ObjectEditor", "Script", "Скрипт");
	add("ObjectEditor", "Edit", "Редактировать");
	add("ObjectEditor", "Content", "Содержимое");
	add("ObjectEditor", "Target URL", "Целевой URL");
	add("ObjectEditor", "Portal Target URL", "Целевой URL портала");
	add("ObjectEditor", "Walk through the portal to travel to this sub:// destination.", "Пройдите через портал, чтобы перейти по этому адресу sub://.");
	add("ObjectEditor", "<a href=\"#boo\">Open URL in web browser</a>", "<a href=\"#boo\">Открыть URL в браузере</a>");
	add("ObjectEditor", "Materials", "Материалы");
	add("ObjectEditor", "Current mat:", "Текущий мат.:");
	add("ObjectEditor", "New Material", "Новый материал");
	add("ObjectEditor", "Material 0 (Inner Rim)", "Материал 0 (Внутренний обод)");
	add("ObjectEditor", "Material 1 (Arch Body)", "Материал 1 (Тело арки)");
	add("ObjectEditor", "Material 2 (Outer Edge)", "Материал 2 (Внешний кант)");
	add("ObjectEditor", "Material 3 (Portal Effect)", "Материал 3 (Эффект портала)");
	add("ObjectEditor", "Material 4 (Threshold)", "Материал 4 (Порог)");
	add("ObjectEditor", "Lightmap", "Лайтмапа");
	add("ObjectEditor", "Lightmap URL:", "URL лайтмапы:");
	add("ObjectEditor", "Bake lightmap (fast)", "Запечь лайтмапу (быстро)");
	add("ObjectEditor", "Bake lightmap (high qual)", "Запечь лайтмапу (высокое качество)");
	add("ObjectEditor", "Video", "Видео");
	add("ObjectEditor", "Autoplay", "Автовоспроизведение");
	add("ObjectEditor", "Loop", "Повтор");
	add("ObjectEditor", "Muted", "Без звука");
	add("ObjectEditor", "URL", "URL");
	add("ObjectEditor", "Volume", "Громкость");
	add("ObjectEditor", "Physics", "Физика");
	add("ObjectEditor", "Collidable", "Коллизия");
	add("ObjectEditor", "Dynamic", "Динамический");
	add("ObjectEditor", "Sensor", "Сенсор");
	add("ObjectEditor", "Mass", "Масса");
	add("ObjectEditor", "Friction", "Трение");
	add("ObjectEditor", "Restitution (elasticity)", "Упругость");
	add("ObjectEditor", "Centre of mass offset", "Смещение центра масс");
	add("ObjectEditor", "Audio", "Аудио");
	add("ObjectEditor", "Audio Player", "Аудио-плеер");
	add("ObjectEditor", "Audio File", "Аудиофайл");
	add("ObjectEditor", "Shuffle", "Перемешивание");
	add("ObjectEditor", "Activation Distance", "Дистанция активации");
	add("ObjectEditor", "Sound Radius", "Радиус звука");
	add("ObjectEditor", "3D Directionality", "3D направленность");
	add("ObjectEditor", "Directionality Focus", "Фокус направленности");
	add("ObjectEditor", "Directionality Sharpness", "Резкость направленности");
	add("ObjectEditor", "Directional Spread", "Угол направленности");
	add("ObjectEditor", "Use Daily Schedule", "Использовать расписание");
	add("ObjectEditor", "Schedule Start", "Начало расписания");
	add("ObjectEditor", "Schedule End", "Конец расписания");
	add("ObjectEditor", "Playlist", "Плейлист");
	add("ObjectEditor", "Add Tracks", "Добавить треки");
	add("ObjectEditor", "Add URL", "Добавить URL");
	add("ObjectEditor", "Remove", "Удалить");
	add("ObjectEditor", "Up", "Вверх");
	add("ObjectEditor", "Down", "Вниз");
	add("ObjectEditor", "Add Playlist Entry", "Добавить элемент плейлиста");
	add("ObjectEditor", "Audio/Radio stream URL or local path:", "URL аудио/радиопотока или локальный путь:");
	add("ObjectEditor", "Playback volume for this audio player.", "Громкость воспроизведения этого аудио-плеера.");
	add("ObjectEditor", "Enable automatic playback when this object loads.", "Автоматически запускать воспроизведение при загрузке объекта.");
	add("ObjectEditor", "Loop the playlist when it reaches the end.", "Зацикливать плейлист при достижении конца.");
	add("ObjectEditor", "Shuffle playlist order.", "Перемешивать порядок воспроизведения плейлиста.");
	add("ObjectEditor", "Distance from camera where the player becomes active.", "Расстояние от камеры, на котором плеер становится активным.");
	add("ObjectEditor", "Maximum audible distance for this player's sound.", "Максимальная слышимая дистанция звука этого плеера.");
	add("ObjectEditor", "Enable directional 3D sound cone.", "Включить направленный 3D-конус звука.");
	add("ObjectEditor", "Forward focus of directional sound pattern.", "Фокус вперёд для диаграммы направленности звука.");
	add("ObjectEditor", "Sharpness of directional sound attenuation.", "Резкость затухания направленного звука.");
	add("ObjectEditor", "Angular spread of directional sound in degrees.", "Угловая ширина направленного звука в градусах.");
	add("ObjectEditor", "Restrict playback to a daily time window.", "Ограничить воспроизведение ежедневным временным окном.");
	add("ObjectEditor", "Playback start time in local hours.", "Локальное время начала воспроизведения (часы).");
	add("ObjectEditor", "Playback end time in local hours.", "Локальное время окончания воспроизведения (часы).");
	add("ObjectEditor", "Tracks and stream URLs played by this audio player.", "Треки и потоковые URL, которые воспроизводит этот аудио-плеер.");
	add("ObjectEditor", "Spotlight", "Прожектор");
	add("ObjectEditor", "Luminous flux:", "Световой поток:");
	add("ObjectEditor", "Colour", "Цвет");
	add("ObjectEditor", "Start angle", "Начальный угол");
	add("ObjectEditor", "End angle", "Конечный угол");
	add("ObjectEditor", "Generic object", "Обычный объект");
	add("ObjectEditor", "Hypercard", "Гиперкарточка");
	add("ObjectEditor", "Voxel Group", "Воксельная группа");
	add("ObjectEditor", "Web View", "Web View");
	add("ObjectEditor", "Video", "Видео");
	add("ObjectEditor", "Text", "Текст");
	add("ObjectEditor", "Portal", "Портал");
	add("ObjectEditor", "created by", "создан");
	add("ObjectEditor", "last modified", "изменён");
	add("ObjectEditor", "user id: ", "id пользователя: ");
	add("ObjectEditor", "[Unknown]", "[Неизвестно]");
	add("ObjectEditor", "Lightmap is baking...", "Лайтмапа запекается...");
	add("ObjectEditor", "Lightmap baked.", "Лайтмапа запечена.");

	// World settings
	add("WorldSettingsWidget", "New terrain section", "Новая секция рельефа");
	add("WorldSettingsWidget", "detail col map 0 (rock) URL", "URL карты цвета detail 0 (камень)");
	add("WorldSettingsWidget", "detail col map 1 (sediment) URL", "URL карты цвета detail 1 (осадок)");
	add("WorldSettingsWidget", "detail col map 2 (vegetation) URL", "URL карты цвета detail 2 (растительность)");
	add("WorldSettingsWidget", "detail col map 3 URL", "URL карты цвета detail 3");
	add("WorldSettingsWidget", "detail height map 0 (rock) URL", "URL карты высот detail 0 (камень)");
	add("WorldSettingsWidget", "terrain section width", "Ширина секции рельефа");
	add("WorldSettingsWidget", "default terrain z", "Базовая высота рельефа (z)");
	add("WorldSettingsWidget", "terrain height scale", "Масштаб высоты рельефа");
	add("WorldSettingsWidget", "water z coord", "Координата воды по z");
	add("WorldSettingsWidget", "water", "Вода");
	add("WorldSettingsWidget", "Apply", "Применить");
	add("WorldSettingsWidget", "Sun", "Солнце");
	add("WorldSettingsWidget", "Sun angle from vertical", "Угол солнца от вертикали");
	add("WorldSettingsWidget", "Sun azimuthal angle", "Азимутальный угол солнца");
	add("WorldSettingsWidget", "Fog", "Туман");
	add("WorldSettingsWidget", "Layer 0 thickness", "Плотность слоя 0");
	add("WorldSettingsWidget", "Layer 0 height scale", "Масштаб высоты слоя 0");
	add("WorldSettingsWidget", "Layer 1 thickness", "Плотность слоя 1");
	add("WorldSettingsWidget", "Layer 1 height scale", "Масштаб высоты слоя 1");

	add("TerrainSpecSectionWidget", "Height map URL", "URL карты высот");
	add("TerrainSpecSectionWidget", "Mask map URL", "URL карты маски");
	add("TerrainSpecSectionWidget", "Tree mask map URL", "URL карты маски деревьев");
	add("TerrainSpecSectionWidget", "Remove", "Удалить");

	// Environment options
	add("EnvironmentOptionsWidget", "Sun angle from vertical", "Угол солнца от вертикали");
	add("EnvironmentOptionsWidget", "Sun azimuthal angle", "Азимутальный угол солнца");
	add("EnvironmentOptionsWidget", "Use local sun direction", "Использовать локальное направление солнца");
	add("EnvironmentOptionsWidget", "Northern Lights", "Северное сияние");
	add("EnvironmentOptionsWidget", " deg", " град");

	// MaterialEditor
	add("MaterialEditor", "Colour", "Цвет");
	add("MaterialEditor", "Texture", "Текстура");
	add("MaterialEditor", "Texture x scale", "Масштаб текстуры по x");
	add("MaterialEditor", "Texture y scale", "Масштаб текстуры по y");
	add("MaterialEditor", "Roughness", "Шероховатость");
	add("MaterialEditor", "Metallic Fraction", "Доля металличности");
	add("MaterialEditor", "Metallic roughness texture", "Текстура металличности/шероховатости");
	add("MaterialEditor", "Normal map", "Карта нормалей");
	add("MaterialEditor", "Opacity", "Прозрачность");
	add("MaterialEditor", "Emission Colour", "Цвет эмиссии");
	add("MaterialEditor", "Emission Texture", "Текстура эмиссии");
	add("MaterialEditor", "Luminance", "Яркость");
	add("MaterialEditor", "Hologram", "Голограмма");
	add("MaterialEditor", "Use vert colours for wind", "Использовать цвета вершин для ветра");
	add("MaterialEditor", "Double sided", "Двусторонний");
	add("MaterialEditor", "Decal", "Декаль");

	// Add model/image dialog
	add("AddObjectDialog", "Add Model or Image", "Добавить модель или изображение");
	add("AddObjectDialog", "Model library", "Библиотека моделей");
	add("AddObjectDialog", "<html><head/><body><p>Supported model formats: OBJ, GLTF, GLB, VOX, STL and IGMESH</p><p>Supported image formats: JPG, PNG, GIF, EXR, KTX, KTX2, BASIS, TIF, TIFF, BMP, TGA, WEBP</p></body></html>",
		"<html><head/><body><p>Поддерживаемые форматы моделей: OBJ, GLTF, GLB, VOX, STL и IGMESH</p><p>Поддерживаемые форматы изображений: JPG, PNG, GIF, EXR, KTX, KTX2, BASIS, TIF, TIFF, BMP, TGA, WEBP</p></body></html>");
	add("AddObjectDialog", "From disk", "С диска");
	add("AddObjectDialog", "URL", "URL");
	add("AddObjectDialog", "From Web", "Из Web");
	add("AddObjectDialog", "Preview:", "Предпросмотр:");

	// Avatar settings
	add("AvatarSettingsWidget", "Model:", "Модель:");
	add("AvatarSettingsWidget", "Preview animation", "Анимация предпросмотра");
	add("AvatarSettingsWidget", "Avatar", "Аватар");
	add("AvatarSettingsWidget", "VRoid: not logged in", "VRoid: вход не выполнен");
	add("AvatarSettingsWidget", "Login", "Войти");
	add("AvatarSettingsWidget", "Fetch models", "Получить модели");
	add("AvatarSettingsWidget", "Logout", "Выйти");
	add("AvatarSettingsWidget", "VRoid workflow: log in, fetch models, then download VRM/GLB in browser and select it on the Avatar tab.",
		"Процесс VRoid: войдите, получите список моделей, затем скачайте VRM/GLB в браузере и выберите файл на вкладке «Аватар».");
	add("AvatarSettingsWidget", "<a href=\"https://hub.vroid.com/\">Open VRoid Hub</a>", "<a href=\"https://hub.vroid.com/\">Открыть VRoid Hub</a>");
	add("AvatarSettingsWidget", "VRoid", "VRoid");
	add("AvatarSettingsWidget", "Create a ReadyPlayerMe avatar", "Создать аватар ReadyPlayerMe");
	add("AvatarSettingsWidget", "Create a AvaturnMe avatar", "Создать аватар AvaturnMe");
	add("AvatarSettingsWidget", "After creating, download and select in file browser above.",
		"После создания скачайте файл и выберите его в браузере файлов выше.");

	add("AvatarSettingsDialog", "Avatar Settings", "Настройки аватара");
	add("AvatarSettingsDialog", "Your name:", "Ваше имя:");
	add("AvatarSettingsDialog", "Model:", "Модель:");
	add("AvatarSettingsDialog", "Preview animation", "Анимация предпросмотра");
	add("AvatarSettingsDialog", "Create ReadyPlayerme avatars at XXX", "Создайте аватары ReadyPlayerMe на XXX");

	// VRoid auth status/error strings shown in Avatar settings.
	add("VRoidAuthFlow", "VRoid: waiting for browser login...", "VRoid: ожидание входа в браузере...");
	add("VRoidAuthFlow", "VRoid: exchanging token...", "VRoid: обмен токеном...");
	add("VRoidAuthFlow", "VRoid: logged in.", "VRoid: вход выполнен.");
	add("VRoidAuthFlow", "VRoid: logged out.", "VRoid: выход выполнен.");
	add("VRoidAuthFlow", "VRoid: not logged in.", "VRoid: вход не выполнен.");
	add("VRoidAuthFlow", "VRoid: not logged in", "VRoid: вход не выполнен");
	add("VRoidAuthFlow", "VRoid: login failed", "VRoid: ошибка входа");
	add("VRoidAuthFlow", "VRoid: settings is null", "VRoid: настройки недоступны");
	add("VRoidAuthFlow", "VRoid OAuth callback missing code.", "VRoid OAuth: в callback отсутствует code.");
	add("VRoidAuthFlow", "VRoid OAuth state mismatch.", "VRoid OAuth: несовпадение state.");
	add("VRoidAuthFlow", "VRoid OAuth is not configured (client_id/client_secret missing).",
		"VRoid OAuth не настроен (client_id/client_secret отсутствуют).");

	// About dialog
	add("AboutDialog", "About", "О программе");
	add("AboutDialog", "Generate Crash", "Сгенерировать сбой");
	add("AboutDialog", "Metasiberia v%1", "Metasiberia v%1");
	add("AboutDialog", "Metasiberia is inspired and based on <a href=\"https://www.glaretechnologies.com/\"><span style=\" text-decoration: underline; color:#222222;\">Glare-core</span></a>.<br>",
		"Metasiberia вдохновлена и основана на <a href=\"https://www.glaretechnologies.com/\"><span style=\" text-decoration: underline; color:#222222;\">Glare-core</span></a>.<br>");
	add("AboutDialog", "Author: <a href=\"https://x.com/denshipilovart\"><span style=\" text-decoration: underline; color:#222222;\">Denis Shipilov</span></a>",
		"Автор: <a href=\"https://x.com/denshipilovart\"><span style=\" text-decoration: underline; color:#222222;\">Denis Shipilov</span></a>");
}


QString RuntimeTranslator::translate(const char* context, const char* sourceText, const char* /*disambiguation*/, int /*n*/) const
{
	if(!context || !sourceText)
		return QString();

	return ru_translations.value(key(context, sourceText), QString());
}


bool RuntimeTranslator::isEmpty() const
{
	return ru_translations.isEmpty();
}
