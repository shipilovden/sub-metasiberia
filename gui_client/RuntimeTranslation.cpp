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
	add("MainWindow", "View", "Вид");
	add("MainWindow", "Go", "Переход");
	add("MainWindow", "Tools", "Инструменты");
	add("MainWindow", "Window", "Окно");
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

	// Toolbar / actions
	add("MainWindow", "Add Model / Image", "Добавить модель / изображение");
	add("MainWindow", "Add Video", "Добавить видео");
	add("MainWindow", "Add Hypercard", "Добавить гиперкарточку");
	add("MainWindow", "Add Voxels", "Добавить воксели");
	add("MainWindow", "Add Web View", "Добавить Web View");
	add("MainWindow", "Show Parcels", "Показать участки");
	add("MainWindow", "Avatar Settings", "Настройки аватара");
	add("MainWindow", "About Metasiberia", "О Metasiberia");
	add("MainWindow", "Add Text", "Добавить текст");
	add("MainWindow", "Add Spotlight", "Добавить прожектор");
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
	add("MainWindow", "Enter new name:", "Введите новое имя:");
	add("MainWindow", "Favorite renamed.", "Избранное переименовано.");
	add("MainWindow", "Delete", "Удалить");
	add("MainWindow", "Delete Favorite", "Удалить избранное");
	add("MainWindow", "Are you sure you want to delete this favorite?", "Вы уверены, что хотите удалить это избранное?");
	add("MainWindow", "Favorite deleted.", "Избранное удалено.");
	add("MainWindow", "(No favorites)", "(Нет избранного)");
	add("MainWindow", "Use the W/A/S/D keys and arrow keys to move and look around.\n", "Используйте клавиши W/A/S/D и стрелки для передвижения и обзора.\n");
	add("MainWindow", "Click and drag the mouse on the 3D view to look around.\n", "Нажмите и перетаскивайте мышь в 3D-окне для обзора.\n");
	add("MainWindow", "Space key: jump\n", "Пробел: прыжок\n");
	add("MainWindow", "Double-click an object to select it.", "Дважды щёлкните по объекту, чтобы выбрать его.");

	// FileSelectWidget
	add("FileSelectWidget", "Browse", "Обзор");

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
	add("ObjectEditor", "<a href=\"#boo\">Open URL in web browser</a>", "<a href=\"#boo\">Открыть URL в браузере</a>");
	add("ObjectEditor", "Materials", "Материалы");
	add("ObjectEditor", "Current mat:", "Текущий мат.:");
	add("ObjectEditor", "New Material", "Новый материал");
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
	add("ObjectEditor", "Audio File", "Аудиофайл");
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

	// About dialog
	add("AboutDialog", "About", "О программе");
	add("AboutDialog", "Generate Crash", "Сгенерировать сбой");
	add("AboutDialog", "Metasiberia v%1", "Metasiberia v%1");
	add("AboutDialog", "Metasiberia is inspired and based on <a href=\"https://substrata.info\"><span style=\" text-decoration: underline; color:#222222;\">Substrata</span></a>.<br>",
		"Metasiberia вдохновлена и основана на <a href=\"https://substrata.info\"><span style=\" text-decoration: underline; color:#222222;\">Substrata</span></a>.<br>");
	add("AboutDialog", "Forked by <a href=\"https://x.com/denshipilovart\"><span style=\" text-decoration: underline; color:#222222;\">Denis Shipilov</span></a>",
		"Форк от <a href=\"https://x.com/denshipilovart\"><span style=\" text-decoration: underline; color:#222222;\">Denis Shipilov</span></a>");
}


QString RuntimeTranslator::translate(const char* context, const char* sourceText, const char* /*disambiguation*/, int /*n*/) const
{
	if(!context || !sourceText)
		return QString();

	return ru_translations.value(key(context, sourceText), QString());
}
