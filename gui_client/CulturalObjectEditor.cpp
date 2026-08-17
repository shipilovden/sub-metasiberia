/*=====================================================================
CulturalObjectEditor.cpp
------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "CulturalObjectEditor.h"

#include "CulturalApiClient.h"


#include "../qt/QtUtils.h"
#include "../utils/Exception.h"
#include <maths/matrix3.h>
#include <maths/mathstypes.h>
#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListView>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>


namespace
{
static std::string stdString(const QString& s) { const QByteArray b = s.toUtf8(); return std::string(b.constData(), (size_t)b.size()); }
static QString qString(const std::string& s) { return QString::fromUtf8(s.data(), (int)s.size()); }


static QString culturalApiProviderName(const QString& provider_id)
{
	if(provider_id == QStringLiteral("artic"))
		return QString::fromUtf8("Институт искусств Чикаго");
	if(provider_id == QStringLiteral("met"))
		return QString::fromUtf8("Музей Метрополитен");
	return QString::fromUtf8("Все подключённые базы");
}


static QString culturalApiValueText(const QJsonValue& value)
{
	if(value.isString())
		return value.toString().trimmed();
	if(value.isDouble())
		return QString::number(value.toDouble(), 'g', 15);
	if(value.isBool())
		return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
	if(value.isArray())
	{
		QStringList values;
		for(const QJsonValue& item : value.toArray())
		{
			const QString text = culturalApiValueText(item);
			if(!text.isEmpty())
				values.push_back(text);
		}
		return values.join(QStringLiteral("; "));
	}
	if(value.isObject())
	{
		const QJsonObject object = value.toObject();
		for(const QString& key : { QStringLiteral("title"), QStringLiteral("name"), QStringLiteral("displayName"), QStringLiteral("value") })
		{
			const QString text = culturalApiValueText(object.value(key));
			if(!text.isEmpty())
				return text;
		}
	}
	return QString();
}


static QString culturalApiField(const QJsonObject& object, const QStringList& keys)
{
	for(const QString& key : keys)
	{
		const QString value = culturalApiValueText(object.value(key));
		if(!value.isEmpty())
			return value;
	}
	return QString();
}


static QString culturalApiDetails(const CulturalApiRecord& record)
{
	const QJsonObject& object = record.raw;
	QStringList lines;
	lines.push_back(QString::fromUtf8("Источник: ") + culturalApiProviderName(record.provider_id));
	const auto add = [&lines, &object](const QString& label, const QStringList& keys) {
		const QString value = culturalApiField(object, keys);
		if(!value.isEmpty())
			lines.push_back(label + QStringLiteral(": ") + value);
	};
	add(QString::fromUtf8("Название"), { QStringLiteral("title"), QStringLiteral("objectName") });
	add(QString::fromUtf8("Автор"), { QStringLiteral("artist_display"), QStringLiteral("artistDisplayName"), QStringLiteral("artist_title") });
	add(QString::fromUtf8("Дата"), { QStringLiteral("date_display"), QStringLiteral("objectDate") });
	add(QString::fromUtf8("Описание"), { QStringLiteral("description"), QStringLiteral("short_description"), QStringLiteral("objectDescription") });
	add(QString::fromUtf8("Отдел / коллекция"), { QStringLiteral("department_title"), QStringLiteral("department"), QStringLiteral("repository") });
	add(QString::fromUtf8("Инвентарный номер"), { QStringLiteral("main_reference_number"), QStringLiteral("accessionNumber"), QStringLiteral("objectID") });
	add(QString::fromUtf8("Классификация"), { QStringLiteral("classification_titles"), QStringLiteral("classification_title"), QStringLiteral("classification"), QStringLiteral("objectName") });
	add(QString::fromUtf8("Материалы"), { QStringLiteral("medium_display"), QStringLiteral("medium"), QStringLiteral("material_titles") });
	add(QString::fromUtf8("Техники"), { QStringLiteral("technique_titles") });
	add(QString::fromUtf8("Стиль"), { QStringLiteral("style_titles"), QStringLiteral("style_title") });
	add(QString::fromUtf8("Темы / сюжеты"), { QStringLiteral("theme_titles"), QStringLiteral("subject_titles"), QStringLiteral("tags") });
	add(QString::fromUtf8("Культура / период"), { QStringLiteral("culture"), QStringLiteral("period"), QStringLiteral("dynasty") });
	add(QString::fromUtf8("География"), { QStringLiteral("place_of_origin"), QStringLiteral("country"), QStringLiteral("city"), QStringLiteral("region") });
	add(QString::fromUtf8("Размеры"), { QStringLiteral("dimensions") });
	add(QString::fromUtf8("Кредит / атрибуция"), { QStringLiteral("credit_line"), QStringLiteral("creditLine"), QStringLiteral("copyright_notice") });
	add(QString::fromUtf8("Права источника"), { QStringLiteral("license_text"), QStringLiteral("copyright_notice") });
	lines.push_back(record.public_domain ? QString::fromUtf8("Правовой режим: public domain подтверждён источником.") : QString::fromUtf8("Правовой режим: public domain не подтверждён источником."));
	if(!record.iiif_manifest_url.isEmpty())
		lines.push_back(QStringLiteral("IIIF manifest: ") + record.iiif_manifest_url);
	if(!record.preview_url.isEmpty())
		lines.push_back(QStringLiteral("Public image: ") + record.preview_url);
	if(!record.source_url.isEmpty())
		lines.push_back(QStringLiteral("URL: ") + record.source_url);
	lines.push_back(QString::fromUtf8("Кнопка «Полные данные / JSON» получает исходную полную запись музея без сохранения её целиком в WorldObject."));
	return lines.join(QStringLiteral("\n\n"));
}


static QPixmap culturalApiPreview(const CulturalApiRecord& record)
{
	const QJsonObject thumbnail = record.raw.value(QStringLiteral("thumbnail")).toObject();
	const QString lqip = thumbnail.value(QStringLiteral("lqip")).toString();
	const int comma = lqip.indexOf(QLatin1Char(','));
	if(comma < 0 || !lqip.left(comma).contains(QStringLiteral("base64"), Qt::CaseInsensitive))
		return QPixmap();
	QImage image;
	if(!image.loadFromData(QByteArray::fromBase64(lqip.mid(comma + 1).toLatin1())))
		return QPixmap();
	return QPixmap::fromImage(image);
}
}


CulturalObjectEditor::CulturalObjectEditor(QWidget* parent)
:	QWidget(parent),
	editing_ob_uid(UID(0)),
	controls_editable(false),
	syncing(false),
	show_3d_controls(NULL), link_scale(NULL), snap_to_grid(NULL), grid_spacing(NULL),
	pos_x(NULL), pos_y(NULL), pos_z(NULL), scale_x(NULL), scale_y(NULL), scale_z(NULL), rot_x(NULL), rot_y(NULL), rot_z(NULL)
{
	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(7);
	QLabel* heading = new QLabel(QString::fromUtf8("<b>MetaSiberia. Редактор объектов культуры</b>"), this);
	heading->setTextFormat(Qt::RichText);
	root->addWidget(heading);
	info_label = new QLabel(QString::fromUtf8("Объект культуры: новый объект"), this);
	info_label->setWordWrap(true);
	info_label->setFrameShape(QFrame::StyledPanel);
	root->addWidget(info_label);
	status_label = new QLabel(QString::fromUtf8("Введите название и найдите объект по подключённым музейным базам."), this);
	status_label->setWordWrap(true);
	root->addWidget(status_label);

	tabs = new QTabWidget(this);
	tabs->setUsesScrollButtons(true);
	root->addWidget(tabs, 1);
	makeCatalogueTab(artic_catalogue, QStringLiteral("ArtIC"), /*is_artic=*/true);
	makeCatalogueTab(met_catalogue, QStringLiteral("The Met"), /*is_artic=*/false);

	QGridLayout* g = NULL;
	QWidget* settings_tab = makeTab(QString::fromUtf8("Настройки"), &g);
	int row = 0;
	addLine(g, row, "title", QString::fromUtf8("Название"));
	addCombo(g, row, "object_type", QString::fromUtf8("Тип объекта"), {
		QString::fromUtf8("Живопись|painting"), QString::fromUtf8("Графика|graphics"), QString::fromUtf8("Скульптура|sculpture"),
		QString::fromUtf8("Археологический объект|archaeological_object"), QString::fromUtf8("Декоративно-прикладное искусство|decorative_applied_art"),
		QString::fromUtf8("Архитектура|architecture"), QString::fromUtf8("Фотография|photography"), QString::fromUtf8("Костюм и мода|fashion"),
		QString::fromUtf8("Книга|book"), QString::fromUtf8("Рукопись|manuscript"), QString::fromUtf8("Архивный документ|archive_document"),
		QString::fromUtf8("Музыкальный инструмент|musical_instrument"), QString::fromUtf8("Музыкальное произведение|musical_work"),
		QString::fromUtf8("Аудиозапись|audio_recording"), QString::fromUtf8("Театральный объект|theatre"), QString::fromUtf8("Кино|film"),
		QString::fromUtf8("Видеоарт|video_art"), QString::fromUtf8("Медиа-арт|media_art"), QString::fromUtf8("Цифровое искусство|digital_art"),
		QString::fromUtf8("Интерактивная инсталляция|interactive_installation"), QString::fromUtf8("Этнографический объект|ethnographic_object"),
		QString::fromUtf8("Нематериальное наследие|intangible_heritage"), QString::fromUtf8("Памятник|monument"),
		QString::fromUtf8("Культурная территория|cultural_territory"), QString::fromUtf8("Исторический объект|historical_object"),
		QString::fromUtf8("Музейный экспонат|museum_exhibit"), QString::fromUtf8("Пользовательский объект|custom") });
	addLine(g, row, "cultural_category", QString::fromUtf8("Категория"));
	addText(g, row, "description", QString::fromUtf8("Описание"), 90);
	QLineEdit* uuid = addLine(g, row, "uuid", QStringLiteral("UUID")); uuid->setReadOnly(true);
	addCombo(g, row, "source_mode", QString::fromUtf8("Источник данных"), { QString::fromUtf8("Вручную|manual"), QString::fromUtf8("Локальный файл|local"), QStringLiteral("URL|url"), QString::fromUtf8("Онлайн-база|online") });
	addLine(g, row, "local_file", QString::fromUtf8("Локальный файл"));
	addLine(g, row, "source_url", QStringLiteral("URL"));
	addCombo(g, row, "provider_id", QString::fromUtf8("Онлайн-база"), { QString::fromUtf8("Все подключённые базы|all"), QString::fromUtf8("Институт искусств Чикаго|artic"), QString::fromUtf8("Музей Метрополитен|met"), QString::fromUtf8("Вручную|manual") });
	addLine(g, row, "provider_record_id", QString::fromUtf8("ID записи источника"));
	QHBoxLayout* source_buttons = new QHBoxLayout();
	QPushButton* browse = new QPushButton(QString::fromUtf8("Выбрать файл"), settings_tab);
	QPushButton* import = new QPushButton(QString::fromUtf8("Импортировать"), settings_tab);
	QPushButton* search = new QPushButton(QString::fromUtf8("Найти в онлайн-базах"), settings_tab);
	QPushButton* refresh = new QPushButton(QString::fromUtf8("Обновить данные"), settings_tab);
	source_buttons->addWidget(browse); source_buttons->addWidget(import); source_buttons->addWidget(search); source_buttons->addWidget(refresh);
	g->addLayout(source_buttons, row++, 0, 1, 2);
	QHBoxLayout* record_buttons = new QHBoxLayout();
	QPushButton* merge = new QPushButton(QString::fromUtf8("Объединить записи"), settings_tab);
	QPushButton* raw = new QPushButton(QString::fromUtf8("Показать JSON"), settings_tab);
	QPushButton* license = new QPushButton(QString::fromUtf8("Проверить лицензию"), settings_tab);
	record_buttons->addWidget(merge); record_buttons->addWidget(raw); record_buttons->addWidget(license);
	g->addLayout(record_buttons, row++, 0, 1, 2);
	connect(browse, &QPushButton::clicked, this, [this]() { const QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8("Выберите данные объекта культуры"), QString(), QStringLiteral("JSON / IIIF (*.json);;Media and models (*.jpg *.jpeg *.png *.glb *.gltf *.obj *.mp3 *.wav *.mp4);;All files (*.*)")); if(!path.isEmpty()) { lines["local_file"]->setText(path); combos["source_mode"]->setCurrentIndex(combos["source_mode"]->findData(QStringLiteral("local"))); } });
	connect(import, &QPushButton::clicked, this, &CulturalObjectEditor::importLocalJson);
	connect(raw, &QPushButton::clicked, this, &CulturalObjectEditor::showRawJson);
	connect(search, &QPushButton::clicked, this, &CulturalObjectEditor::searchOnline);
	connect(refresh, &QPushButton::clicked, this, &CulturalObjectEditor::searchOnline);
	connect(merge, &QPushButton::clicked, this, [this]() { setStatus(QString::fromUtf8("Для объединения нужны минимум две записи источников. Entity Resolution входит в следующий этап.")); });
	connect(license, &QPushButton::clicked, this, [this]() { const QString v = combo("license_status"); setStatus(v == QStringLiteral("unknown_license") ? QString::fromUtf8("Лицензия неизвестна: импорт медиа должен быть подтверждён пользователем.") : QString::fromUtf8("Статус лицензии: %1").arg(v), v == QStringLiteral("blocked")); });

	QWidget* data_tab = makeTab(QString::fromUtf8("Данные"), &g); row = 0;
	addText(g,row,"alternative_titles",QString::fromUtf8("Альтернативные названия")); addText(g,row,"creators",QString::fromUtf8("Авторы / создатели"));
	addLine(g,row,"creation_date",QString::fromUtf8("Дата создания")); addLine(g,row,"country",QString::fromUtf8("Страна")); addLine(g,row,"place_of_creation",QString::fromUtf8("Место создания"));
	addLine(g,row,"current_location",QString::fromUtf8("Текущее место хранения")); addLine(g,row,"collection",QString::fromUtf8("Коллекция / музей")); addLine(g,row,"inventory_number",QString::fromUtf8("Инвентарный номер"));
	addText(g,row,"art_forms",QString::fromUtf8("Виды искусства")); addText(g,row,"museum_classifications",QString::fromUtf8("Музейные классификации")); addText(g,row,"disciplines",QString::fromUtf8("Научные дисциплины"));
	addLine(g,row,"cultures",QString::fromUtf8("Культура")); addLine(g,row,"periods",QString::fromUtf8("Период")); addLine(g,row,"materials",QString::fromUtf8("Материалы")); addLine(g,row,"techniques",QString::fromUtf8("Техники"));
	addLine(g,row,"styles",QString::fromUtf8("Стиль")); addLine(g,row,"genres",QString::fromUtf8("Жанр")); addLine(g,row,"functions",QString::fromUtf8("Функция")); addText(g,row,"subjects",QString::fromUtf8("Темы / сюжеты")); addLine(g,row,"keywords",QString::fromUtf8("Ключевые слова"));
	addLine(g,row,"wikidata_id",QStringLiteral("Wikidata ID")); addLine(g,row,"iiif_id",QStringLiteral("IIIF ID")); addLine(g,row,"museum_id",QString::fromUtf8("Музейный ID")); addLine(g,row,"europeana_id",QStringLiteral("Europeana ID"));
	Q_UNUSED(data_tab);

	QWidget* card_tab = makeTab(QString::fromUtf8("Карточка объекта"), &g); row = 0;
	addLine(g,row,"card_title",QString::fromUtf8("Заголовок")); addLine(g,row,"card_subtitle",QString::fromUtf8("Подзаголовок")); addText(g,row,"card_summary",QString::fromUtf8("Краткое описание"),100);
	addCombo(g,row,"card_theme",QString::fromUtf8("Тема"),{QString::fromUtf8("Тёмная|dark"),QString::fromUtf8("Светлая|light"),QString::fromUtf8("Музейная|museum")});
	addLine(g,row,"card_language",QString::fromUtf8("Язык")); addLine(g,row,"card_visible_fields",QString::fromUtf8("Отображаемые поля")); addDouble(g,row,"card_scale",QString::fromUtf8("Масштаб"),0.05,20.0,0.05);
	addCheck(g,row,"card_auto_open",QString::fromUtf8("Открывать автоматически")); addCheck(g,row,"card_open_on_click",QString::fromUtf8("Открывать по клику")); addCheck(g,row,"card_pinned",QString::fromUtf8("Закрепить рядом с объектом")); addText(g,row,"plaque_text",QString::fromUtf8("Текст музейной таблички"),110);
	Q_UNUSED(card_tab);

	QWidget* media_tab = makeTab(QString::fromUtf8("Медиа"), &g); row = 0;
	addLine(g,row,"primary_image_url",QString::fromUtf8("Главное изображение")); addLine(g,row,"high_resolution_image_url",QString::fromUtf8("Высокое разрешение")); addLine(g,row,"iiif_manifest_url",QStringLiteral("IIIF manifest"));
	addLine(g,row,"audio_url",QString::fromUtf8("Аудио / аудиогид")); addLine(g,row,"video_url",QString::fromUtf8("Видео")); addText(g,row,"documents",QString::fromUtf8("Документы")); addLine(g,row,"media_cache_key",QString::fromUtf8("Ключ кэша")); addCheck(g,row,"lazy_media_loading",QString::fromUtf8("Ленивая загрузка"));
	addCombo(g,row,"license_status",QString::fromUtf8("Статус лицензии"),{QStringLiteral("free_use|free_use"),QStringLiteral("attribution_required|attribution_required"),QStringLiteral("non_commercial|non_commercial"),QStringLiteral("restricted|restricted"),QStringLiteral("metadata_only|metadata_only"),QStringLiteral("unknown_license|unknown_license"),QStringLiteral("blocked|blocked")});
	addLine(g,row,"rights_holder",QString::fromUtf8("Правообладатель")); addLine(g,row,"license_url",QString::fromUtf8("URL лицензии")); addText(g,row,"attribution_text",QString::fromUtf8("Атрибуция"));
	addCheck(g,row,"allow_display",QString::fromUtf8("Разрешено отображение")); addCheck(g,row,"allow_download",QString::fromUtf8("Разрешено скачивание")); addCheck(g,row,"allow_modify",QString::fromUtf8("Разрешено изменение")); addCheck(g,row,"allow_commercial_use",QString::fromUtf8("Разрешено коммерческое использование"));
	Q_UNUSED(media_tab);

	QWidget* history_tab = makeTab(QString::fromUtf8("История"), &g); row = 0;
	addText(g,row,"provenance",QStringLiteral("Provenance / владельцы"),130); addText(g,row,"exhibitions",QString::fromUtf8("Выставки"),100); addText(g,row,"restorations",QString::fromUtf8("Реставрации"),100); addText(g,row,"condition",QString::fromUtf8("Состояние")); addText(g,row,"publications",QString::fromUtf8("Публикации"),100); addText(g,row,"related_objects",QString::fromUtf8("Связанные объекты"));
	Q_UNUSED(history_tab);

	QWidget* exhibition_tab = makeTab(QString::fromUtf8("Трансформация"), &g); row = 0;
	addLine(g,row,"model_3d_url",QString::fromUtf8("Модель (Quad)"));
	show_3d_controls = new QCheckBox(QString::fromUtf8("Показывать 3D-контролы позиции/поворота"), exhibition_tab); g->addWidget(show_3d_controls,row++,0,1,2);
	addLine(g,row,"exhibition_scene",QString::fromUtf8("Сцена")); addLine(g,row,"exhibition_room",QString::fromUtf8("Зал")); addLine(g,row,"exhibition_zone",QString::fromUtf8("Зона"));
	QLabel* axes = new QLabel(QStringLiteral("X / Y / Z"), exhibition_tab); g->addWidget(axes,row,1); row++;
	pos_x=addDouble(g,row,"_pos_x",QString::fromUtf8("Позиция X"),-1e9,1e9,0.05,4); pos_y=addDouble(g,row,"_pos_y",QString::fromUtf8("Позиция Y"),-1e9,1e9,0.05,4); pos_z=addDouble(g,row,"_pos_z",QString::fromUtf8("Позиция Z"),-1e9,1e9,0.05,4);
	rot_x=addDouble(g,row,"_rot_x",QString::fromUtf8("Поворот X"),-360000,360000,1.0,2); rot_y=addDouble(g,row,"_rot_y",QString::fromUtf8("Поворот Y"),-360000,360000,1.0,2); rot_z=addDouble(g,row,"_rot_z",QString::fromUtf8("Поворот Z"),-360000,360000,1.0,2);
	scale_x=addDouble(g,row,"_scale_x",QString::fromUtf8("Масштаб X"),0.0001,1e6,0.05,4); scale_y=addDouble(g,row,"_scale_y",QString::fromUtf8("Масштаб Y"),0.0001,1e6,0.05,4); scale_z=addDouble(g,row,"_scale_z",QString::fromUtf8("Масштаб Z"),0.0001,1e6,0.05,4);
	link_scale = new QCheckBox(QString::fromUtf8("Связать масштаб X/Y/Z"), exhibition_tab); link_scale->setChecked(true); g->addWidget(link_scale,row++,0,1,2);
	snap_to_grid = new QCheckBox(QString::fromUtf8("Привязка к сетке"), exhibition_tab); g->addWidget(snap_to_grid,row,0); grid_spacing = new QDoubleSpinBox(exhibition_tab); grid_spacing->setRange(0.001,1000000); grid_spacing->setValue(1.0); grid_spacing->setSuffix(QStringLiteral(" m")); g->addWidget(grid_spacing,row++,1);
	addCombo(g,row,"placement",QString::fromUtf8("Размещение"),{QString::fromUtf8("Свободно|free"),QString::fromUtf8("На стене|wall"),QString::fromUtf8("На подставке|pedestal"),QString::fromUtf8("В витрине|case")});
	addLine(g,row,"frame_style",QString::fromUtf8("Рама")); addLine(g,row,"pedestal_style",QString::fromUtf8("Подставка")); addLine(g,row,"case_style",QString::fromUtf8("Витрина")); addCheck(g,row,"spotlight",QString::fromUtf8("Прожектор")); addDouble(g,row,"light_intensity",QString::fromUtf8("Интенсивность света"),0,100,0.05,2); addCheck(g,row,"shadows",QString::fromUtf8("Тени")); addCheck(g,row,"interactive",QString::fromUtf8("Интерактивность")); addDouble(g,row,"activation_distance",QString::fromUtf8("Дистанция активации"),0,10000,0.25,2);
	addLine(g,row,"route_id",QString::fromUtf8("Маршрут")); addLine(g,row,"route_stop",QString::fromUtf8("Номер остановки")); addLine(g,row,"next_object_uuid",QString::fromUtf8("Следующий объект UUID")); addText(g,row,"curator_note",QString::fromUtf8("Кураторская заметка"),100);

	connectChangeSignals();
	connect(show_3d_controls, &QCheckBox::toggled, this, [this](bool) { if(!syncing) emit posAndRot3DControlsToggled(); });
	connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
		if(tabs->widget(index) == artic_catalogue.page_widget && !artic_catalogue.has_searched)
			searchCatalogue(artic_catalogue, /*reset_page=*/true);
	});
	// ArtIC is the first tab.  currentChanged() is connected after the tabs are
	// created, so queue the initial browse explicitly instead of making the user
	// switch tabs before the first gallery appears.
	QTimer::singleShot(0, this, [this]() {
		if(!artic_catalogue.has_searched)
			searchCatalogue(artic_catalogue, /*reset_page=*/true);
	});
}


QWidget* CulturalObjectEditor::makeCatalogueTab(CatalogueControls& catalogue, const QString& title, bool is_artic)
{
	catalogue.provider_id = is_artic ? QStringLiteral("artic") : QStringLiteral("met");
	QWidget* page = new QWidget(tabs);
	catalogue.page_widget = page;
	QVBoxLayout* root_layout = new QVBoxLayout(page);
	root_layout->setContentsMargins(6, 6, 6, 6);
	root_layout->setSpacing(0);
	QSplitter* catalogue_splitter = new QSplitter(Qt::Horizontal, page);
	catalogue_splitter->setChildrenCollapsible(false);
	// The filter pane must be able to grow to the right.  The gallery remains
	// reachable through the splitter handle, but is allowed to collapse when the
	// user deliberately gives the filters all available width.
	catalogue_splitter->setCollapsible(0, false);
	catalogue_splitter->setCollapsible(1, true);
	catalogue_splitter->setOpaqueResize(true);
	catalogue_splitter->setHandleWidth(10);

	QScrollArea* filters_scroll = new QScrollArea(catalogue_splitter);
	filters_scroll->setWidgetResizable(true);
	filters_scroll->setMinimumWidth(190);
	filters_scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	QWidget* filters = new QWidget(filters_scroll);
	QVBoxLayout* filters_layout = new QVBoxLayout(filters);
	filters_layout->setContentsMargins(8, 8, 8, 8);
	QLabel* source_hint = new QLabel(is_artic
		? QString::fromUtf8("<b>Институт искусств Чикаго</b><br/>Поиск и фильтры официального ArtIC API. Карточки используют публичный IIIF-предпросмотр.<br/><small>Выберите значение из списка или введите свой термин ArtIC — это позволяет искать авторов и категории, которых нет среди быстрых вариантов.</small>")
		: QString::fromUtf8("<b>Музей Метрополитен</b><br/>Поиск и фильтры официального The Met API. Для этого источника требуется запрос."), filters);
	source_hint->setWordWrap(true);
	filters_layout->addWidget(source_hint);
	QPushButton* search = NULL;
	QPushButton* reset_filters = NULL;
	if(is_artic)
	{
		// These are intentional, visible catalogue facets rather than hidden text
		// syntax.  Display labels are Russian, while userData holds the exact
		// English ArtIC term passed to the public API.
		const auto add_section = [&filters_layout, filters](const QString& text) {
			QLabel* label = new QLabel(QStringLiteral("<b>%1</b>").arg(text), filters);
			label->setTextFormat(Qt::RichText);
			label->setStyleSheet(QStringLiteral("color: #a9bad2; margin-top: 8px;"));
			filters_layout->addWidget(label);
		};
		const auto add_selector = [&filters_layout, filters](const QString& label_text, QComboBox*& selector) {
			QLabel* label = new QLabel(label_text, filters);
			selector = new QComboBox(filters);
			// The visible list gives the common values, but the ArtIC catalogue is
			// much larger.  Editable selectors keep every API term reachable without
			// pretending this curated list is a complete museum taxonomy.
			selector->setEditable(true);
			selector->setInsertPolicy(QComboBox::NoInsert);
			selector->setMaxVisibleItems(20);
			selector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
			selector->setMinimumContentsLength(20);
			filters_layout->addWidget(label);
			filters_layout->addWidget(selector);
		};
		const auto add_choice = [](QComboBox* selector, const QString& label, const QString& api_value) {
			selector->addItem(label, api_value);
		};

		add_section(QString::fromUtf8("ПОИСК И АВТОРЫ"));
		QLabel* query_label = new QLabel(QString::fromUtf8("Название / ключевое слово"), filters);
		catalogue.query = new QLineEdit(filters);
		catalogue.query->setPlaceholderText(QString::fromUtf8("Название…"));
		filters_layout->addWidget(query_label);
		filters_layout->addWidget(catalogue.query);
		add_selector(QString::fromUtf8("Автор"), catalogue.artist_selector);
		add_choice(catalogue.artist_selector, QString::fromUtf8("Все художники"), QString());
		add_choice(catalogue.artist_selector, QString::fromUtf8("Винсент ван Гог"), QStringLiteral("Vincent van Gogh"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Клод Моне"), QStringLiteral("Claude Monet"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Пабло Пикассо"), QStringLiteral("Pablo Picasso"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Джорджия О’Киф"), QStringLiteral("Georgia O'Keeffe"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Рембрандт"), QStringLiteral("Rembrandt"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Жорж Сёра"), QStringLiteral("Georges Seurat"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Пьер-Огюст Ренуар"), QStringLiteral("Pierre-Auguste Renoir"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Эдгар Дега"), QStringLiteral("Edgar Degas"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Анри Матисс"), QStringLiteral("Henri Matisse"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Марк Шагал"), QStringLiteral("Marc Chagall"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Густав Климт"), QStringLiteral("Gustav Klimt"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Энди Уорхол"), QStringLiteral("Andy Warhol"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Кацусика Хокусай"), QStringLiteral("Katsushika Hokusai"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Анри де Тулуз-Лотрек"), QStringLiteral("Henri de Toulouse-Lautrec"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Мэри Кассат"), QStringLiteral("Mary Cassatt"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Эдвард Хоппер"), QStringLiteral("Edward Hopper"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Поль Сезанн"), QString::fromUtf8("Paul Cézanne"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Сальвадор Дали"), QString::fromUtf8("Salvador Dalí"));
		add_choice(catalogue.artist_selector, QString::fromUtf8("Фрида Кало"), QStringLiteral("Frida Kahlo"));

		add_section(QString::fromUtf8("НАПРАВЛЕНИЯ"));
		add_selector(QString::fromUtf8("Стиль / направление"), catalogue.style_selector);
		add_choice(catalogue.style_selector, QString::fromUtf8("Любой стиль"), QString());
		add_choice(catalogue.style_selector, QString::fromUtf8("Импрессионизм"), QStringLiteral("Impressionism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Постимпрессионизм"), QStringLiteral("Post-Impressionism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Модернизм"), QStringLiteral("Modernism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Барокко"), QStringLiteral("Baroque"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Ренессанс"), QStringLiteral("Renaissance"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Сюрреализм"), QStringLiteral("Surrealism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Абстракционизм"), QStringLiteral("Abstractionism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Кубизм"), QStringLiteral("Cubism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Фовизм"), QStringLiteral("Fauvism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Ар-нуво"), QStringLiteral("Art Nouveau"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Ар-деко"), QStringLiteral("Art Deco"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Экспрессионизм"), QStringLiteral("Expressionism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Поп-арт"), QStringLiteral("Pop Art"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Минимализм"), QStringLiteral("Minimalism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Рококо"), QStringLiteral("Rococo"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Классицизм"), QStringLiteral("Classicism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Символизм"), QStringLiteral("Symbolism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Реализм"), QStringLiteral("Realism"));
		add_choice(catalogue.style_selector, QString::fromUtf8("Маньеризм"), QStringLiteral("Mannerism"));

		add_section(QString::fromUtf8("ТЕМЫ И РЕГИОНЫ"));
		add_selector(QString::fromUtf8("Тема / сюжет"), catalogue.theme_selector);
		add_choice(catalogue.theme_selector, QString::fromUtf8("Любая тема"), QString());
		add_choice(catalogue.theme_selector, QStringLiteral("mythology"), QStringLiteral("mythology"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Кошки"), QStringLiteral("cats"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Собаки"), QStringLiteral("dogs"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Птицы"), QStringLiteral("birds"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Дикие животные"), QStringLiteral("wild animals"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Цветы"), QStringLiteral("flowers"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Портреты"), QStringLiteral("portrait"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Пейзажи"), QStringLiteral("landscape"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Натюрморты"), QStringLiteral("still life"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Море"), QStringLiteral("sea"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Корабли"), QStringLiteral("ships"));
		add_choice(catalogue.theme_selector, QStringLiteral("Париж"), QStringLiteral("Paris"));
		add_choice(catalogue.theme_selector, QStringLiteral("Япония"), QStringLiteral("Japan"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Женщины"), QStringLiteral("women"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Мужчины"), QStringLiteral("men"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Дети"), QStringLiteral("children"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Ночь"), QStringLiteral("night"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Архитектура"), QStringLiteral("architecture"));
		add_choice(catalogue.theme_selector, QString::fromUtf8("Руины"), QStringLiteral("ruins"));
		add_selector(QString::fromUtf8("Регион / место"), catalogue.region_selector);
		add_choice(catalogue.region_selector, QString::fromUtf8("Все регионы"), QString());
		add_choice(catalogue.region_selector, QStringLiteral("Франция"), QStringLiteral("France"));
		add_choice(catalogue.region_selector, QStringLiteral("Италия"), QStringLiteral("Italy"));
		add_choice(catalogue.region_selector, QStringLiteral("Китай"), QStringLiteral("China"));
		add_choice(catalogue.region_selector, QStringLiteral("Япония"), QStringLiteral("Japan"));
		add_choice(catalogue.region_selector, QStringLiteral("Корея"), QStringLiteral("Korea"));
		add_choice(catalogue.region_selector, QStringLiteral("Индия"), QStringLiteral("India"));
		add_choice(catalogue.region_selector, QString::fromUtf8("Древний Египет"), QStringLiteral("Egypt"));
		add_choice(catalogue.region_selector, QString::fromUtf8("Древняя Греция"), QStringLiteral("Greece"));
		add_choice(catalogue.region_selector, QString::fromUtf8("Древний Рим"), QStringLiteral("Roman"));
		add_choice(catalogue.region_selector, QString::fromUtf8("Месопотамия"), QStringLiteral("Mesopotamia"));
		add_choice(catalogue.region_selector, QString::fromUtf8("Византия"), QStringLiteral("Byzantine"));
		add_choice(catalogue.region_selector, QString::fromUtf8("Америка"), QStringLiteral("America"));
		add_choice(catalogue.region_selector, QStringLiteral("Мексика"), QStringLiteral("Mexico"));
		add_choice(catalogue.region_selector, QStringLiteral("Перу"), QStringLiteral("Peru"));
		add_choice(catalogue.region_selector, QString::fromUtf8("Нидерланды"), QStringLiteral("Netherlands"));
		add_choice(catalogue.region_selector, QStringLiteral("Испания"), QStringLiteral("Spain"));
		add_choice(catalogue.region_selector, QString::fromUtf8("Англия"), QStringLiteral("England"));

		add_section(QString::fromUtf8("МАТЕРИАЛЫ И ВРЕМЯ"));
		add_selector(QString::fromUtf8("Материал / техника"), catalogue.material_selector);
		add_choice(catalogue.material_selector, QString::fromUtf8("Все материалы"), QString());
		add_choice(catalogue.material_selector, QString::fromUtf8("Масло"), QStringLiteral("oil"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Акварель"), QStringLiteral("watercolor"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Темпера"), QStringLiteral("tempera"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Фреска"), QStringLiteral("fresco"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Коллаж"), QStringLiteral("collage"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Офорт"), QStringLiteral("etching"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Литография"), QStringLiteral("lithograph"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Ксилография"), QStringLiteral("woodcut"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Бронза"), QStringLiteral("bronze"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Мрамор"), QStringLiteral("marble"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Холст"), QStringLiteral("canvas"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Бумага"), QStringLiteral("paper"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Пергамент"), QStringLiteral("parchment"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Слоновая кость"), QStringLiteral("ivory"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Терракота"), QStringLiteral("terracotta"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Известняк"), QStringLiteral("limestone"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Папирус"), QStringLiteral("papyrus"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Дерево"), QStringLiteral("wood"));
		add_choice(catalogue.material_selector, QString::fromUtf8("Золото"), QStringLiteral("gold"));
		add_selector(QString::fromUtf8("Период / время"), catalogue.period_selector);
		add_choice(catalogue.period_selector, QString::fromUtf8("Любой период"), QString());
		add_choice(catalogue.period_selector, QString::fromUtf8("До 1500 года"), QStringLiteral("-5000:1499"));
		add_choice(catalogue.period_selector, QString::fromUtf8("16 век"), QStringLiteral("1500:1599"));
		add_choice(catalogue.period_selector, QString::fromUtf8("17 век"), QStringLiteral("1600:1699"));
		add_choice(catalogue.period_selector, QString::fromUtf8("18 век"), QStringLiteral("1700:1799"));
		add_choice(catalogue.period_selector, QString::fromUtf8("19 век"), QStringLiteral("1800:1899"));
		add_choice(catalogue.period_selector, QString::fromUtf8("20 век"), QStringLiteral("1900:1999"));
		add_choice(catalogue.period_selector, QString::fromUtf8("Современность"), QStringLiteral("2000:2100"));

		add_section(QString::fromUtf8("МУЗЕЙНЫЕ ОТДЕЛЫ"));
		add_selector(QString::fromUtf8("Музейный отдел"), catalogue.department_selector);
		add_choice(catalogue.department_selector, QString::fromUtf8("Все отделы"), QString());
		add_choice(catalogue.department_selector, QString::fromUtf8("Европейская живопись и скульптура"), QStringLiteral("Painting and Sculpture of Europe"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Современное искусство"), QStringLiteral("Modern and Contemporary Art"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Искусство Азии"), QStringLiteral("Arts of Asia"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Фотография и медиа"), QStringLiteral("Photography and Media"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Дизайн и архитектура"), QStringLiteral("Architecture and Design"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Искусство Африки"), QStringLiteral("Arts of Africa"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Греция, Рим и Византия"), QStringLiteral("Arts of Greece, Rome, and Byzantium"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Гравюры и рисунки"), QStringLiteral("Prints and Drawings"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Текстиль"), QStringLiteral("Textiles"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Прикладное искусство Европы"), QStringLiteral("Applied Arts of Europe"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Искусство Америки"), QStringLiteral("Arts of the Americas"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Современное искусство (Contemporary)"), QStringLiteral("Contemporary Art"));
		add_choice(catalogue.department_selector, QString::fromUtf8("Модерн"), QStringLiteral("Modern Art"));
		add_selector(QString::fromUtf8("Тип объекта"), catalogue.object_type_selector);
		add_choice(catalogue.object_type_selector, QString::fromUtf8("Все типы"), QString());
		add_choice(catalogue.object_type_selector, QString::fromUtf8("Живопись"), QStringLiteral("painting"));
		add_choice(catalogue.object_type_selector, QString::fromUtf8("Скульптура"), QStringLiteral("sculpture"));
		add_choice(catalogue.object_type_selector, QString::fromUtf8("Гравюра"), QStringLiteral("print"));
		add_choice(catalogue.object_type_selector, QString::fromUtf8("Рисунок"), QStringLiteral("drawing"));
		add_choice(catalogue.object_type_selector, QString::fromUtf8("Фотография"), QStringLiteral("photograph"));
		add_choice(catalogue.object_type_selector, QString::fromUtf8("Текстиль"), QStringLiteral("textile"));
		add_choice(catalogue.object_type_selector, QString::fromUtf8("Архитектура"), QStringLiteral("architecture"));
		add_choice(catalogue.object_type_selector, QString::fromUtf8("Декоративное искусство"), QStringLiteral("decorative arts"));
		add_choice(catalogue.object_type_selector, QString::fromUtf8("Мебель"), QStringLiteral("furniture"));
		add_choice(catalogue.object_type_selector, QString::fromUtf8("Рукопись"), QStringLiteral("manuscript"));

		add_section(QString::fromUtf8("МЕДИА-КОНТЕНТ"));
		catalogue.has_images = new QCheckBox(QString::fromUtf8("Только с изображением"), filters);
		catalogue.audio = new QCheckBox(QString::fromUtf8("Аудио"), filters);
		catalogue.video = new QCheckBox(QString::fromUtf8("Видео"), filters);
		catalogue.has_images->setChecked(true);
		filters_layout->addWidget(catalogue.has_images);
		filters_layout->addWidget(catalogue.audio);
		filters_layout->addWidget(catalogue.video);

		add_section(QString::fromUtf8("ДОПОЛНИТЕЛЬНО"));
		catalogue.public_domain = new QCheckBox(QString::fromUtf8("Только свободный доступ (public domain)"), filters);
		// Search the whole public catalogue by default.  The checkbox is an
		// explicit reuse restriction: non-public records remain visible as
		// metadata/LQIP previews, but still cannot be added to the world.
		catalogue.public_domain->setChecked(false);
		filters_layout->addWidget(catalogue.public_domain);
		search = new QPushButton(QString::fromUtf8("Показать объекты"), filters);
		filters_layout->addWidget(search);

		QGroupBox* rights_box = new QGroupBox(QString::fromUtf8("Права, CC0 и IIIF"), filters);
		QVBoxLayout* rights_layout = new QVBoxLayout(rights_box);
		QLabel* rights = new QLabel(QString::fromUtf8(
			"<b>Источник:</b> <a href=\"https://www.artic.edu/\">Институт искусств Чикаго</a> · "
			"<a href=\"https://api.artic.edu/docs/\">документация API</a><br/><br/>"
			"<b>Метаданные:</b> для Artwork API все данные, кроме поля <code>description</code>, "
			"публикуются под <a href=\"https://creativecommons.org/publicdomain/zero/1.0/\">CC0 1.0</a>. "
			"Поле description имеет CC BY 4.0; условия источника остаются обязательными.<br/><br/>"
			"<b>Изображения:</b> ArtIC отдаёт их через <a href=\"https://iiif.io/api/image/2.0/\">IIIF Image API 2.0</a>. "
			"В мир автоматически добавляются только записи с <code>is_public_domain=true</code> и публичным image_id. "
			"<a href=\"https://www.artic.edu/image-licensing\">Лицензирование изображений ArtIC</a>."), rights_box);
		rights->setTextFormat(Qt::RichText);
		rights->setTextInteractionFlags(Qt::TextBrowserInteraction);
		rights->setOpenExternalLinks(true);
		rights->setWordWrap(true);
		rights_layout->addWidget(rights);
		filters_layout->addWidget(rights_box);
		reset_filters = new QPushButton(QString::fromUtf8("Сбросить все фильтры"), filters);
		filters_layout->addWidget(reset_filters);
	}
	else
	{
		QFormLayout* form = new QFormLayout();
		form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
		filters_layout->addLayout(form);
		const auto add_filter = [&form, filters](const QString& label, QLineEdit*& field, const QString& placeholder) {
			field = new QLineEdit(filters);
			field->setPlaceholderText(placeholder);
			form->addRow(label, field);
		};
		add_filter(QString::fromUtf8("Название / ключевое слово"), catalogue.query, QString::fromUtf8("Например: Van Gogh"));
		add_filter(QString::fromUtf8("Автор / культура"), catalogue.artist, QString::fromUtf8("Автор, мастер, культура"));
		add_filter(QString::fromUtf8("Регион / география"), catalogue.region, QString::fromUtf8("Egypt"));
		add_filter(QString::fromUtf8("Материал"), catalogue.material, QString::fromUtf8("Gold"));
		add_filter(QString::fromUtf8("Department ID"), catalogue.department, QString::fromUtf8("11"));
		QHBoxLayout* dates = new QHBoxLayout();
		catalogue.date_begin = new QSpinBox(filters);
		catalogue.date_end = new QSpinBox(filters);
		for(QSpinBox* spin : { catalogue.date_begin, catalogue.date_end })
		{
			spin->setRange(-5000, 2100);
			spin->setSpecialValueText(QString::fromUtf8("Любой"));
			spin->setValue(0);
		}
		dates->addWidget(catalogue.date_begin);
		dates->addWidget(catalogue.date_end);
		form->addRow(QString::fromUtf8("Годы от / до"), dates);
		catalogue.public_domain = new QCheckBox(QString::fromUtf8("Только public domain"), filters);
		catalogue.has_images = new QCheckBox(QString::fromUtf8("Только с изображением"), filters);
		catalogue.public_domain->setChecked(true);
		catalogue.has_images->setChecked(true);
		catalogue.title_only = new QCheckBox(QString::fromUtf8("Искать только в названии"), filters);
		catalogue.artist_or_culture = new QCheckBox(QString::fromUtf8("Искать автора / культуру"), filters);
		catalogue.on_view = new QCheckBox(QString::fromUtf8("Только экспонируется сейчас"), filters);
		catalogue.highlights = new QCheckBox(QString::fromUtf8("Только highlights"), filters);
		filters_layout->addWidget(catalogue.public_domain);
		filters_layout->addWidget(catalogue.has_images);
		filters_layout->addWidget(catalogue.title_only);
		filters_layout->addWidget(catalogue.artist_or_culture);
		filters_layout->addWidget(catalogue.on_view);
		filters_layout->addWidget(catalogue.highlights);
		search = new QPushButton(QString::fromUtf8("Показать объекты"), filters);
		filters_layout->addWidget(search);
	}
	filters_layout->addStretch(1);
	filters_scroll->setWidget(filters);
	catalogue_splitter->addWidget(filters_scroll);

	QWidget* results_widget = new QWidget(catalogue_splitter);
	// Do not let the preview/details controls impose a fixed minimum width on
	// the splitter.  They can reflow or clip while the user drags the divider.
	results_widget->setMinimumSize(0, 0);
	results_widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	QVBoxLayout* results_layout = new QVBoxLayout(results_widget);
	results_layout->setContentsMargins(0, 0, 0, 0);
	results_layout->setSizeConstraint(QLayout::SetNoConstraint);
	catalogue.status = new QLabel(QString::fromUtf8("Настройте фильтры и нажмите «Показать объекты»."), results_widget);
	catalogue.status->setWordWrap(true);
	results_layout->addWidget(catalogue.status);
	catalogue.result_list = new QListWidget(results_widget);
	catalogue.result_list->setViewMode(QListView::IconMode);
	catalogue.result_list->setResizeMode(QListView::Adjust);
	catalogue.result_list->setMovement(QListView::Static);
	catalogue.result_list->setWordWrap(true);
	catalogue.result_list->setIconSize(QSize(170, 128));
	catalogue.result_list->setGridSize(QSize(195, 190));
	catalogue.result_list->setSelectionMode(QAbstractItemView::SingleSelection);
	catalogue.result_list->setMinimumWidth(0);
	catalogue.result_list->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	results_layout->addWidget(catalogue.result_list, 2);

	QHBoxLayout* page_buttons = new QHBoxLayout();
	catalogue.previous_page = new QPushButton(QString::fromUtf8("← Предыдущая"), results_widget);
	catalogue.next_page = new QPushButton(QString::fromUtf8("Следующая →"), results_widget);
	page_buttons->addWidget(catalogue.previous_page);
	page_buttons->addWidget(catalogue.next_page);
	page_buttons->addStretch(1);
	results_layout->addLayout(page_buttons);

	QHBoxLayout* selected_layout = new QHBoxLayout();
	catalogue.preview = new QLabel(QString::fromUtf8("Выберите объект для предпросмотра"), results_widget);
	catalogue.preview->setAlignment(Qt::AlignCenter);
	catalogue.preview->setMinimumSize(0, 160);
	catalogue.preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	catalogue.preview->setFrameShape(QFrame::StyledPanel);
	catalogue.preview->setWordWrap(true);
	selected_layout->addWidget(catalogue.preview);
	QVBoxLayout* metadata_layout = new QVBoxLayout();
	catalogue.details = new QPlainTextEdit(results_widget);
	catalogue.details->setReadOnly(true);
	catalogue.details->setMinimumWidth(0);
	catalogue.details->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	catalogue.details->setMinimumHeight(165);
	metadata_layout->addWidget(catalogue.details, 1);
	QHBoxLayout* action_buttons = new QHBoxLayout();
	QPushButton* full_json = new QPushButton(QString::fromUtf8("Полные данные / JSON"), results_widget);
	QPushButton* apply = new QPushButton(QString::fromUtf8("Применить к текущему"), results_widget);
	QPushButton* add = new QPushButton(QString::fromUtf8("Добавить в мир"), results_widget);
	action_buttons->addWidget(full_json);
	action_buttons->addWidget(apply);
	action_buttons->addWidget(add);
	metadata_layout->addLayout(action_buttons);
	selected_layout->addLayout(metadata_layout, 1);
	results_layout->addLayout(selected_layout);
	catalogue_splitter->addWidget(results_widget);
	catalogue_splitter->setStretchFactor(0, 0);
	catalogue_splitter->setStretchFactor(1, 1);
	catalogue_splitter->setSizes(QList<int>() << 270 << 640);
	// Make the catalogue divider an explicit drag target: LMB on this handle
	// resizes the filters and the image gallery without clipping either panel.
	catalogue_splitter->handle(1)->setCursor(Qt::SplitHCursor);
	root_layout->addWidget(catalogue_splitter, 1);

	CatalogueControls* catalogue_ptr = &catalogue;
	connect(search, &QPushButton::clicked, this, [this, catalogue_ptr]() { searchCatalogue(*catalogue_ptr, /*reset_page=*/true); });
	if(catalogue.artist_selector)
	{
		connect(catalogue.artist_selector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [catalogue_ptr](int index) {
			// A chosen author is normally the user's new starting point.  Clear
			// stale refinements such as a previous style or object type, otherwise a
			// perfectly valid author can misleadingly appear to have no artworks.
			if(index <= 0)
				return;
			for(QComboBox* selector : { catalogue_ptr->style_selector, catalogue_ptr->theme_selector,
				catalogue_ptr->region_selector, catalogue_ptr->material_selector, catalogue_ptr->period_selector,
				catalogue_ptr->department_selector, catalogue_ptr->object_type_selector })
			{
				if(selector)
					selector->setCurrentIndex(0);
			}
			if(catalogue_ptr->audio)
				catalogue_ptr->audio->setChecked(false);
			if(catalogue_ptr->video)
				catalogue_ptr->video->setChecked(false);
		});
	}
	if(reset_filters)
	{
		connect(reset_filters, &QPushButton::clicked, this, [this, catalogue_ptr]() {
			if(catalogue_ptr->query)
				catalogue_ptr->query->clear();
			for(QComboBox* selector : { catalogue_ptr->artist_selector, catalogue_ptr->style_selector,
				catalogue_ptr->theme_selector, catalogue_ptr->region_selector, catalogue_ptr->material_selector,
				catalogue_ptr->period_selector, catalogue_ptr->department_selector, catalogue_ptr->object_type_selector })
			{
				if(selector)
					selector->setCurrentIndex(0);
			}
			if(catalogue_ptr->public_domain)
				catalogue_ptr->public_domain->setChecked(false);
			if(catalogue_ptr->has_images)
				catalogue_ptr->has_images->setChecked(true);
			if(catalogue_ptr->audio)
				catalogue_ptr->audio->setChecked(false);
			if(catalogue_ptr->video)
				catalogue_ptr->video->setChecked(false);
			searchCatalogue(*catalogue_ptr, /*reset_page=*/true);
		});
	}
	connect(catalogue.previous_page, &QPushButton::clicked, this, [this, catalogue_ptr]() { if(catalogue_ptr->page > 1) searchCatalogue(*catalogue_ptr, /*reset_page=*/false); });
	connect(catalogue.next_page, &QPushButton::clicked, this, [this, catalogue_ptr]() { ++catalogue_ptr->page; searchCatalogue(*catalogue_ptr, /*reset_page=*/false); });
	connect(catalogue.result_list, &QListWidget::currentRowChanged, this, [this, catalogue_ptr](int) { updateCatalogueDetails(*catalogue_ptr); });
	connect(catalogue.result_list, &QListWidget::itemDoubleClicked, this, [this, catalogue_ptr](QListWidgetItem*) { importSelectedCatalogueRecord(*catalogue_ptr); });
	connect(full_json, &QPushButton::clicked, this, [this, catalogue_ptr]() { showSelectedCatalogueJson(*catalogue_ptr); });
	connect(apply, &QPushButton::clicked, this, [this, catalogue_ptr]() { importSelectedCatalogueRecord(*catalogue_ptr); });
	connect(add, &QPushButton::clicked, this, [this, catalogue_ptr]() { addSelectedCatalogueRecord(*catalogue_ptr); });

	tabs->addTab(page, title);
	return page;
}


CulturalApiSearchOptions CulturalObjectEditor::catalogueOptions(const CatalogueControls& catalogue) const
{
	CulturalApiSearchOptions options;
	const auto text_or_empty = [](const QLineEdit* line) { return line ? line->text().trimmed() : QString(); };
	const auto selector_or_text = [&text_or_empty](const QComboBox* selector, const QLineEdit* line) {
		if(!selector)
			return text_or_empty(line);
		const QString current_text = selector->currentText().trimmed();
		const int current_index = selector->currentIndex();
		// A user-entered term has no matching list row; use the text itself.  A
		// selected translated label, on the other hand, maps to its exact ArtIC
		// term stored in itemData.
		if(current_index < 0 || selector->itemText(current_index) != current_text)
			return current_text;
		return selector->itemData(current_index).toString().trimmed();
	};
	options.query = text_or_empty(catalogue.query);
	options.artist = selector_or_text(catalogue.artist_selector, catalogue.artist);
	options.style = selector_or_text(catalogue.style_selector, catalogue.style);
	options.theme = selector_or_text(catalogue.theme_selector, catalogue.theme);
	options.region = selector_or_text(catalogue.region_selector, catalogue.region);
	options.material = selector_or_text(catalogue.material_selector, catalogue.material);
	options.period = selector_or_text(catalogue.period_selector, catalogue.period);
	options.department = selector_or_text(catalogue.department_selector, catalogue.department);
	options.object_type = selector_or_text(catalogue.object_type_selector, catalogue.object_type);
	options.public_domain_only = catalogue.public_domain && catalogue.public_domain->isChecked();
	options.has_images_only = catalogue.has_images && catalogue.has_images->isChecked();
	options.audio_only = catalogue.audio && catalogue.audio->isChecked();
	options.video_only = catalogue.video && catalogue.video->isChecked();
	options.title_only = catalogue.title_only && catalogue.title_only->isChecked();
	options.artist_or_culture = catalogue.artist_or_culture && catalogue.artist_or_culture->isChecked();
	options.on_view_only = catalogue.on_view && catalogue.on_view->isChecked();
	options.highlights_only = catalogue.highlights && catalogue.highlights->isChecked();
	options.date_begin = catalogue.date_begin ? catalogue.date_begin->value() : 0;
	options.date_end = catalogue.date_end ? catalogue.date_end->value() : 0;
	// ArtIC period choices are a numeric range for the API.  Keep the generic
	// text period only for providers such as The Met, whose date filter is local.
	if(catalogue.period_selector && !options.period.isEmpty())
	{
		const QStringList bounds = options.period.split(QLatin1Char(':'));
		bool begin_ok = false;
		bool end_ok = false;
		if(bounds.size() == 2)
		{
			const int begin = bounds.at(0).toInt(&begin_ok);
			const int end = bounds.at(1).toInt(&end_ok);
			if(begin_ok && end_ok)
			{
				options.date_begin = begin;
				options.date_end = end;
				options.period.clear();
			}
		}
	}
	options.page = catalogue.page;
	options.limit = 12;
	return options;
}


void CulturalObjectEditor::searchCatalogue(CatalogueControls& catalogue, bool reset_page)
{
	if(reset_page)
		catalogue.page = 1;
	const CulturalApiSearchOptions options = catalogueOptions(catalogue);
	// Invalidate pending thumbnail callbacks from the previous result page before
	// replacing the list they address.
	++catalogue.preview_generation;
	if(catalogue.provider_id == QStringLiteral("met") && options.query.isEmpty() && options.artist.isEmpty())
	{
		catalogue.status->setText(QString::fromUtf8("Для The Met введите название, автора или ключевое слово. Полная выгрузка коллекции не выполняется."));
		return;
	}

	catalogue.status->setText(QString::fromUtf8("Загрузка официальных музейных данных…"));
	QApplication::setOverrideCursor(Qt::WaitCursor);
	QString error;
	catalogue.result = CulturalApiClient::search(catalogue.provider_id, options, error);
	QApplication::restoreOverrideCursor();
	catalogue.has_searched = true;
	catalogue.result_list->clear();
	for(size_t i = 0; i < catalogue.result.records.size(); ++i)
	{
		const CulturalApiRecord& record = catalogue.result.records[i];
		QListWidgetItem* item = new QListWidgetItem(record.title, catalogue.result_list);
		item->setData(Qt::UserRole, (int)i);
		item->setToolTip(culturalApiDetails(record));
		QPixmap preview;
		if(!record.preview_url.isEmpty())
			preview = catalogue.preview_cache.value(record.preview_url);
		if(preview.isNull())
			preview = culturalApiPreview(record); // Short-lived LQIP placeholder until the public preview arrives.
		if(!preview.isNull())
			item->setIcon(QIcon(preview));
	}
	catalogue.previous_page->setEnabled(catalogue.page > 1);
	catalogue.next_page->setEnabled(catalogue.result.total_pages == 0 || catalogue.page < catalogue.result.total_pages);
	if(catalogue.result.records.empty())
	{
		catalogue.preview->setPixmap(QPixmap());
		catalogue.preview->setText(QString::fromUtf8("Нет объектов для выбранных фильтров"));
		catalogue.details->clear();
		catalogue.status->setText(error.isEmpty() ? QString::fromUtf8("По выбранным фильтрам ничего не найдено.") : QString::fromUtf8("Ошибка: %1").arg(error));
		return;
	}
	catalogue.status->setText(QString::fromUtf8("Показано %1 объектов; всего в ответе источника: %2. Страница %3%4.")
		.arg((int)catalogue.result.records.size()).arg(catalogue.result.total).arg(catalogue.page)
		.arg(catalogue.result.total_pages > 0 ? QStringLiteral(" / %1").arg(catalogue.result.total_pages) : QString()));
	catalogue.result_list->setCurrentRow(0);
	updateCatalogueDetails(catalogue);
	queueCataloguePreviewFetch(catalogue);
}


void CulturalObjectEditor::queueCataloguePreviewFetch(CatalogueControls& catalogue)
{
	const quint64 generation = catalogue.preview_generation;
	CatalogueControls* const catalogue_ptr = &catalogue;
	QTimer::singleShot(0, this, [this, catalogue_ptr, generation]() { fetchCataloguePreview(catalogue_ptr, generation, 0); });
}


void CulturalObjectEditor::fetchCataloguePreview(CatalogueControls* catalogue, quint64 generation, int index)
{
	if(!catalogue || generation != catalogue->preview_generation || index < 0 || index >= (int)catalogue->result.records.size())
		return;

	const CulturalApiRecord& record = catalogue->result.records[(size_t)index];
	QPixmap preview;
	if(record.public_domain && !record.preview_url.isEmpty())
	{
		preview = catalogue->preview_cache.value(record.preview_url);
		if(preview.isNull())
		{
			QByteArray bytes;
			QString error;
			// This runs one small official IIIF/museum request per event-loop turn,
			// so search results stay usable while thumbnails progressively replace LQIP.
			if(CulturalApiClient::downloadPublicImage(record.preview_url, bytes, error, 8000))
			{
				QImage image;
				if(image.loadFromData(bytes))
				{
					image = image.scaled(QSize(340, 256), Qt::KeepAspectRatio, Qt::SmoothTransformation);
					preview = QPixmap::fromImage(image);
					catalogue->preview_cache.insert(record.preview_url, preview);
				}
			}
		}
	}

	if(!preview.isNull())
	{
		if(QListWidgetItem* item = catalogue->result_list->item(index))
			item->setIcon(QIcon(preview));
		if(catalogue->result_list->currentRow() == index)
			updateCatalogueDetails(*catalogue);
	}

	const int next_index = index + 1;
	if(next_index < (int)catalogue->result.records.size())
		QTimer::singleShot(0, this, [this, catalogue, generation, next_index]() { fetchCataloguePreview(catalogue, generation, next_index); });
}


void CulturalObjectEditor::updateCatalogueDetails(CatalogueControls& catalogue)
{
	QListWidgetItem* item = catalogue.result_list->currentItem();
	if(!item)
		return;
	const int index = item->data(Qt::UserRole).toInt();
	if(index < 0 || index >= (int)catalogue.result.records.size())
		return;
	CulturalApiRecord& record = catalogue.result.records[(size_t)index];
	catalogue.details->setPlainText(culturalApiDetails(record));
	QPixmap pixmap;
	if(!record.preview_url.isEmpty())
		pixmap = catalogue.preview_cache.value(record.preview_url);
	if(pixmap.isNull())
		pixmap = culturalApiPreview(record);
	if(pixmap.isNull())
	{
		catalogue.preview->setPixmap(QPixmap());
		catalogue.preview->setText(record.public_domain ? QString::fromUtf8("Публичный предпросмотр не выдан источником") : QString::fromUtf8("Изображение нельзя автоматически использовать: public domain не подтверждён."));
	}
	else
	{
		catalogue.preview->setText(QString());
		catalogue.preview->setPixmap(pixmap.scaled(catalogue.preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
	}
}


void CulturalObjectEditor::importSelectedCatalogueRecord(CatalogueControls& catalogue)
{
	QListWidgetItem* item = catalogue.result_list->currentItem();
	if(!item)
	{
		setStatus(QString::fromUtf8("Сначала выберите объект в каталоге."), true);
		return;
	}
	const int index = item->data(Qt::UserRole).toInt();
	if(index >= 0 && index < (int)catalogue.result.records.size())
		importProviderRecord(catalogue.result.records[(size_t)index]);
}


void CulturalObjectEditor::showSelectedCatalogueJson(CatalogueControls& catalogue)
{
	QListWidgetItem* item = catalogue.result_list->currentItem();
	if(!item)
		return;
	const int index = item->data(Qt::UserRole).toInt();
	if(index < 0 || index >= (int)catalogue.result.records.size())
		return;
	CulturalApiRecord record = catalogue.result.records[(size_t)index];
	QString error;
	CulturalApiRecord full_record;
	if(CulturalApiClient::fetchRecord(record.provider_id, record.record_id, full_record, error))
		record = full_record;
	else
		setStatus(QString::fromUtf8("Не удалось загрузить полную запись: %1").arg(error), true);

	QDialog dialog(this);
	dialog.setWindowTitle(QString::fromUtf8("Полные данные источника"));
	dialog.resize(900, 680);
	QVBoxLayout* layout = new QVBoxLayout(&dialog);
	QPlainTextEdit* json = new QPlainTextEdit(&dialog);
	json->setReadOnly(true);
	json->setPlainText(QString::fromUtf8(QJsonDocument(record.raw).toJson(QJsonDocument::Indented)));
	layout->addWidget(json, 1);
	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	dialog.exec();
}


void CulturalObjectEditor::addSelectedCatalogueRecord(CatalogueControls& catalogue)
{
	QListWidgetItem* item = catalogue.result_list->currentItem();
	if(!item)
		return;
	const int index = item->data(Qt::UserRole).toInt();
	if(index < 0 || index >= (int)catalogue.result.records.size())
		return;
	const CulturalApiRecord& record = catalogue.result.records[(size_t)index];
	if(!record.public_domain || record.preview_url.isEmpty())
	{
		setStatus(QString::fromUtf8("В мир можно добавить только запись с подтверждённым public domain и публичным изображением источника."), true);
		return;
	}
	emit addCulturalObjectRequested(record.provider_id, record.record_id);
}


QWidget* CulturalObjectEditor::makeTab(const QString& title, QGridLayout** grid_out)
{
	QWidget* page = new QWidget(tabs); QVBoxLayout* layout = new QVBoxLayout(page); layout->setContentsMargins(6,6,6,6);
	QGroupBox* group = new QGroupBox(title, page); QGridLayout* grid = new QGridLayout(group); grid->setColumnStretch(1,1); layout->addWidget(group); layout->addStretch(1);
	tabs->addTab(page,title); *grid_out=grid; return page;
}


QLineEdit* CulturalObjectEditor::addLine(QGridLayout* grid,int& row,const char* key,const QString& label,const QString& placeholder)
{ QLabel* l=new QLabel(label,this); QLineEdit* e=new QLineEdit(this); e->setPlaceholderText(placeholder); grid->addWidget(l,row,0); grid->addWidget(e,row++,1); lines[key]=e; return e; }

QPlainTextEdit* CulturalObjectEditor::addText(QGridLayout* grid,int& row,const char* key,const QString& label,int height)
{ QLabel* l=new QLabel(label,this); QPlainTextEdit* e=new QPlainTextEdit(this); e->setMinimumHeight(height); grid->addWidget(l,row,0,Qt::AlignTop); grid->addWidget(e,row++,1); texts[key]=e; return e; }

QComboBox* CulturalObjectEditor::addCombo(QGridLayout* grid,int& row,const char* key,const QString& label,const QStringList& values)
{ QLabel* l=new QLabel(label,this); QComboBox* c=new QComboBox(this); for(const QString& v:values){const int bar=v.lastIndexOf('|'); c->addItem(bar>=0?v.left(bar):v,bar>=0?v.mid(bar+1):v);} grid->addWidget(l,row,0);grid->addWidget(c,row++,1);combos[key]=c;return c; }

QCheckBox* CulturalObjectEditor::addCheck(QGridLayout* grid,int& row,const char* key,const QString& label)
{ QCheckBox* c=new QCheckBox(label,this);grid->addWidget(c,row++,0,1,2);checks[key]=c;return c; }

QDoubleSpinBox* CulturalObjectEditor::addDouble(QGridLayout* grid,int& row,const char* key,const QString& label,double min,double max,double step,int decimals)
{ QLabel* l=new QLabel(label,this);QDoubleSpinBox* s=new QDoubleSpinBox(this);s->setRange(min,max);s->setSingleStep(step);s->setDecimals(decimals);grid->addWidget(l,row,0);grid->addWidget(s,row++,1);doubles[key]=s;return s; }


void CulturalObjectEditor::connectChangeSignals()
{
	for(auto& p:lines) connect(p.second,&QLineEdit::textEdited,this,[this](const QString&){if(!syncing)emit objectChanged();});
	for(auto& p:texts) connect(p.second,&QPlainTextEdit::textChanged,this,[this](){if(!syncing)emit objectChanged();});
	for(auto& p:combos) connect(p.second,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){if(!syncing)emit objectChanged();});
	for(auto& p:checks) connect(p.second,&QCheckBox::toggled,this,[this](bool){if(!syncing)emit objectChanged();});
	for(auto& p:doubles)
	{
		const bool transform = !p.first.empty() && p.first[0]=='_';
		connect(p.second,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this,transform](double){if(!syncing){if(transform)emit objectTransformChanged();else emit objectChanged();}});
	}
	const auto link_scale_values = [this](QDoubleSpinBox* changed, double value) {
		if(syncing || !link_scale || !link_scale->isChecked())
			return;
		const QSignalBlocker block_x(scale_x);
		const QSignalBlocker block_y(scale_y);
		const QSignalBlocker block_z(scale_z);
		if(changed != scale_x) scale_x->setValue(value);
		if(changed != scale_y) scale_y->setValue(value);
		if(changed != scale_z) scale_z->setValue(value);
		emit objectTransformChanged();
	};
	connect(scale_x, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [link_scale_values, this](double value) { if(!syncing) link_scale_values(scale_x, value); });
	connect(scale_y, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [link_scale_values, this](double value) { if(!syncing) link_scale_values(scale_y, value); });
	connect(scale_z, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [link_scale_values, this](double value) { if(!syncing) link_scale_values(scale_z, value); });
}


QString CulturalObjectEditor::line(const char* key) const { auto i=lines.find(key);return i==lines.end()?QString():i->second->text(); }
QString CulturalObjectEditor::text(const char* key) const { auto i=texts.find(key);return i==texts.end()?QString():i->second->toPlainText(); }
QString CulturalObjectEditor::combo(const char* key) const { auto i=combos.find(key);return i==combos.end()?QString():i->second->currentData().toString(); }
bool CulturalObjectEditor::checked(const char* key) const { auto i=checks.find(key);return i!=checks.end()&&i->second->isChecked(); }
double CulturalObjectEditor::number(const char* key) const { auto i=doubles.find(key);return i==doubles.end()?0.0:i->second->value(); }


void CulturalObjectEditor::setControlsFromSettings(const CulturalObjectSettings& s)
{
	current_settings=s;
#define SET_LINE(f) if(lines.count(#f)) lines[#f]->setText(qString(s.f))
#define SET_TEXT(f) if(texts.count(#f)) texts[#f]->setPlainText(qString(s.f))
#define SET_COMBO(f) if(combos.count(#f)){const int i=combos[#f]->findData(qString(s.f));combos[#f]->setCurrentIndex(i>=0?i:0);}
#define SET_CHECK(f) if(checks.count(#f)) checks[#f]->setChecked(s.f)
#define SET_DOUBLE(f) if(doubles.count(#f)) doubles[#f]->setValue(s.f)
	SET_LINE(uuid);SET_LINE(title);SET_COMBO(object_type);SET_LINE(cultural_category);SET_TEXT(description);SET_COMBO(source_mode);SET_LINE(local_file);SET_LINE(source_url);SET_COMBO(provider_id);SET_LINE(provider_record_id);
	SET_TEXT(alternative_titles);SET_TEXT(creators);SET_LINE(creation_date);SET_LINE(country);SET_LINE(place_of_creation);SET_LINE(current_location);SET_LINE(collection);SET_LINE(inventory_number);SET_TEXT(art_forms);SET_TEXT(museum_classifications);SET_TEXT(disciplines);SET_LINE(cultures);SET_LINE(periods);SET_LINE(materials);SET_LINE(techniques);SET_LINE(styles);SET_LINE(genres);SET_LINE(functions);SET_TEXT(subjects);SET_LINE(keywords);SET_LINE(wikidata_id);SET_LINE(iiif_id);SET_LINE(museum_id);SET_LINE(europeana_id);
	SET_LINE(card_title);SET_LINE(card_subtitle);SET_TEXT(card_summary);SET_COMBO(card_theme);SET_LINE(card_language);SET_LINE(card_visible_fields);SET_TEXT(plaque_text);SET_CHECK(card_auto_open);SET_CHECK(card_open_on_click);SET_CHECK(card_pinned);SET_DOUBLE(card_scale);
	SET_LINE(primary_image_url);SET_LINE(high_resolution_image_url);SET_LINE(iiif_manifest_url);SET_LINE(model_3d_url);SET_LINE(audio_url);SET_LINE(video_url);SET_TEXT(documents);SET_LINE(media_cache_key);SET_CHECK(lazy_media_loading);SET_COMBO(license_status);SET_LINE(rights_holder);SET_LINE(license_url);SET_TEXT(attribution_text);SET_CHECK(allow_display);SET_CHECK(allow_download);SET_CHECK(allow_modify);SET_CHECK(allow_commercial_use);
	SET_TEXT(provenance);SET_TEXT(exhibitions);SET_TEXT(restorations);SET_TEXT(condition);SET_TEXT(publications);SET_TEXT(related_objects);SET_LINE(exhibition_scene);SET_LINE(exhibition_room);SET_LINE(exhibition_zone);SET_COMBO(placement);SET_LINE(frame_style);SET_LINE(pedestal_style);SET_LINE(case_style);SET_CHECK(spotlight);SET_DOUBLE(light_intensity);SET_CHECK(shadows);SET_CHECK(interactive);SET_DOUBLE(activation_distance);SET_LINE(route_id);SET_LINE(route_stop);SET_LINE(next_object_uuid);SET_TEXT(curator_note);
#undef SET_LINE
#undef SET_TEXT
#undef SET_COMBO
#undef SET_CHECK
#undef SET_DOUBLE
}


CulturalObjectSettings CulturalObjectEditor::controlsToSettings() const
{
	CulturalObjectSettings s=current_settings;
#define GET_LINE(f) s.f=stdString(line(#f))
#define GET_TEXT(f) s.f=stdString(text(#f))
#define GET_COMBO(f) s.f=stdString(combo(#f))
#define GET_CHECK(f) s.f=checked(#f)
#define GET_DOUBLE(f) s.f=(float)number(#f)
	GET_LINE(uuid);GET_LINE(title);GET_COMBO(object_type);GET_LINE(cultural_category);GET_TEXT(description);GET_COMBO(source_mode);GET_LINE(local_file);GET_LINE(source_url);GET_COMBO(provider_id);GET_LINE(provider_record_id);
	GET_TEXT(alternative_titles);GET_TEXT(creators);GET_LINE(creation_date);GET_LINE(country);GET_LINE(place_of_creation);GET_LINE(current_location);GET_LINE(collection);GET_LINE(inventory_number);GET_TEXT(art_forms);GET_TEXT(museum_classifications);GET_TEXT(disciplines);GET_LINE(cultures);GET_LINE(periods);GET_LINE(materials);GET_LINE(techniques);GET_LINE(styles);GET_LINE(genres);GET_LINE(functions);GET_TEXT(subjects);GET_LINE(keywords);GET_LINE(wikidata_id);GET_LINE(iiif_id);GET_LINE(museum_id);GET_LINE(europeana_id);
	GET_LINE(card_title);GET_LINE(card_subtitle);GET_TEXT(card_summary);GET_COMBO(card_theme);GET_LINE(card_language);GET_LINE(card_visible_fields);GET_TEXT(plaque_text);GET_CHECK(card_auto_open);GET_CHECK(card_open_on_click);GET_CHECK(card_pinned);GET_DOUBLE(card_scale);
	GET_LINE(primary_image_url);GET_LINE(high_resolution_image_url);GET_LINE(iiif_manifest_url);GET_LINE(model_3d_url);GET_LINE(audio_url);GET_LINE(video_url);GET_TEXT(documents);GET_LINE(media_cache_key);GET_CHECK(lazy_media_loading);GET_COMBO(license_status);GET_LINE(rights_holder);GET_LINE(license_url);GET_TEXT(attribution_text);GET_CHECK(allow_display);GET_CHECK(allow_download);GET_CHECK(allow_modify);GET_CHECK(allow_commercial_use);
	GET_TEXT(provenance);GET_TEXT(exhibitions);GET_TEXT(restorations);GET_TEXT(condition);GET_TEXT(publications);GET_TEXT(related_objects);GET_LINE(exhibition_scene);GET_LINE(exhibition_room);GET_LINE(exhibition_zone);GET_COMBO(placement);GET_LINE(frame_style);GET_LINE(pedestal_style);GET_LINE(case_style);GET_CHECK(spotlight);GET_DOUBLE(light_intensity);GET_CHECK(shadows);GET_CHECK(interactive);GET_DOUBLE(activation_distance);GET_LINE(route_id);GET_LINE(route_stop);GET_LINE(next_object_uuid);GET_TEXT(curator_note);
	s.modified_at=stdString(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
#undef GET_LINE
#undef GET_TEXT
#undef GET_COMBO
#undef GET_CHECK
#undef GET_DOUBLE
	return s;
}


void CulturalObjectEditor::setFromObject(const WorldObject& ob,bool)
{
	syncing=true;editing_ob_uid=ob.uid;std::string error;setControlsFromSettings(CulturalObjectSettings::fromContent(ob.content,&error));setTransformFromObject(ob);
	info_label->setText(QString::fromUtf8("CulturalObject (UID: %1)\n%2 | %3").arg((qulonglong)ob.uid.value()).arg(line("title"),combo("object_type")));
	setStatus(error.empty()?QString::fromUtf8("Готово. Изменения сохраняются обычным обновлением объекта."):qString(error),!error.empty());syncing=false;
}


void CulturalObjectEditor::setTransformFromObject(const WorldObject& ob)
{
	const bool old=syncing;syncing=true;pos_x->setValue(ob.pos.x);pos_y->setValue(ob.pos.y);pos_z->setValue(ob.pos.z);scale_x->setValue(ob.scale.x);scale_y->setValue(ob.scale.y);scale_z->setValue(ob.scale.z);
	const Matrix3f m=Matrix3f::rotationMatrix(normalise(ob.axis),ob.angle);const Vec3f a=m.getAngles();rot_x->setValue(a.x*360/Maths::get2Pi<float>());rot_y->setValue(a.y*360/Maths::get2Pi<float>());rot_z->setValue(a.z*360/Maths::get2Pi<float>());syncing=old;
}


void CulturalObjectEditor::setCachedPrimaryImageURL(const QString& resource_url, const QString& source_url, const QString& error_message)
{
	const bool old_syncing = syncing;
	syncing = true;
	current_settings.primary_image_source_url = stdString(source_url);
	if(resource_url.isEmpty())
	{
		current_settings.primary_image_url.clear();
		lines["primary_image_url"]->clear();
		setStatus(QString::fromUtf8("Метаданные импортированы, но изображение не сохранено в ресурсы мира: %1").arg(error_message), true);
	}
	else
	{
		current_settings.primary_image_url = stdString(resource_url);
		lines["primary_image_url"]->setText(resource_url);
		setStatus(QString::fromUtf8("Запись и публичное изображение импортированы. Картина будет отображена на одностороннем Quad."));
	}
	syncing = old_syncing;
}


void CulturalObjectEditor::writeTransformMembersToObject(WorldObject& ob)
{
	ob.pos=Vec3d(pos_x->value(),pos_y->value(),pos_z->value());ob.scale=Vec3f((float)scale_x->value(),(float)scale_y->value(),(float)scale_z->value());
	const Vec3f angles((float)::degreeToRad(rot_x->value()),(float)::degreeToRad(rot_y->value()),(float)::degreeToRad(rot_z->value()));const Matrix3f m=Matrix3f::fromAngles(angles);m.rotationMatrixToAxisAngle(ob.axis,ob.angle);if(ob.axis.length()<1e-5f){ob.axis=Vec3f(0,0,1);ob.angle=0;}
}


void CulturalObjectEditor::toObject(WorldObject& ob)
{
	const CulturalObjectSettings s=controlsToSettings();const std::string content=CulturalObjectSettings::serialiseToContent(s);if(content.size()>WorldObject::MAX_CONTENT_SIZE){setStatus(QString::fromUtf8("CulturalObject: %1 байт превышает предел %2. Сократите длинные поля, raw/media должны храниться ресурсами.").arg((qulonglong)content.size()).arg((qulonglong)WorldObject::MAX_CONTENT_SIZE),true);return;}if(ob.content!=content)ob.changed_flags|=WorldObject::CONTENT_CHANGED;ob.content=content;writeTransformMembersToObject(ob);current_settings=s;
}


void CulturalObjectEditor::objectLastModifiedUpdated(const WorldObject& ob){info_label->setText(QString::fromUtf8("CulturalObject (UID: %1)\n%2 | %3").arg((qulonglong)ob.uid.value()).arg(line("title"),combo("object_type")));}
void CulturalObjectEditor::objectPickedUp(){pos_x->setEnabled(false);pos_y->setEnabled(false);pos_z->setEnabled(false);}
void CulturalObjectEditor::objectDropped(){pos_x->setEnabled(controls_editable);pos_y->setEnabled(controls_editable);pos_z->setEnabled(controls_editable);}
void CulturalObjectEditor::setControlsEnabled(bool enabled){setEnabled(enabled);}
void CulturalObjectEditor::setControlsEditable(bool editable){controls_editable=editable;setEnabled(true);for(auto&p:lines)if(p.first!="uuid")p.second->setReadOnly(!editable);for(auto&p:texts)p.second->setReadOnly(!editable);for(auto&p:combos)p.second->setEnabled(editable);for(auto&p:checks)p.second->setEnabled(editable);for(auto&p:doubles)p.second->setEnabled(editable);if(link_scale)link_scale->setEnabled(editable);show_3d_controls->setEnabled(true);}
bool CulturalObjectEditor::posAndRot3DControlsEnabled()const{return show_3d_controls&&show_3d_controls->isChecked();}
void CulturalObjectEditor::setPosAndRot3DControlsEnabled(bool enabled){if(show_3d_controls){const QSignalBlocker blocker(show_3d_controls);show_3d_controls->setChecked(enabled);}}
bool CulturalObjectEditor::snapToGridChecked()const{return snap_to_grid&&snap_to_grid->isChecked();}
double CulturalObjectEditor::gridSpacing()const{return grid_spacing?grid_spacing->value():1.0;}


void CulturalObjectEditor::setStatus(const QString& s,bool error){status_label->setText(s);QPalette p=status_label->palette();p.setColor(QPalette::WindowText,error?QColor(QStringLiteral("#FF6B6B")):palette().color(QPalette::WindowText));status_label->setPalette(p);}


void CulturalObjectEditor::searchOnline()
{
	const QString provider = combo("provider_id");
	const QString query = line("title").trimmed();
	if(provider == QStringLiteral("all"))
	{
		if(query.isEmpty())
		{
			setStatus(QString::fromUtf8("Введите название, автора или ключевое слово перед поиском по всем подключённым базам."), true);
			return;
		}
		artic_catalogue.query->setText(query);
		met_catalogue.query->setText(query);
		searchCatalogue(artic_catalogue, /*reset_page=*/true);
		searchCatalogue(met_catalogue, /*reset_page=*/true);
		tabs->setCurrentWidget(artic_catalogue.page_widget);
		setStatus(QString::fromUtf8("Результаты обновлены в отдельных вкладках ArtIC и The Met."));
		return;
	}
	CatalogueControls* catalogue = provider == QStringLiteral("met") ? &met_catalogue : &artic_catalogue;
	if(!query.isEmpty())
		catalogue->query->setText(query);
	tabs->setCurrentWidget(catalogue->page_widget);
	searchCatalogue(*catalogue, /*reset_page=*/true);
	return;

#if 0 // Replaced by the persistent provider catalogue tabs above.
	const QString query = line("title").trimmed();
	if(query.isEmpty())
	{
		setStatus(QString::fromUtf8("Введите в поле «Название» автора, название или ключевое слово для поиска."), true);
		return;
	}

	QString provider = combo("provider_id");
	if(provider == QStringLiteral("manual"))
		provider = QStringLiteral("all");
	setStatus(QString::fromUtf8("Идёт поиск «%1» в «%2»…").arg(query, culturalApiProviderName(provider)));
	QApplication::setOverrideCursor(Qt::WaitCursor);
	QString error;
	const std::vector<CulturalApiRecord> records = CulturalApiClient::search(provider, query, error);
	QApplication::restoreOverrideCursor();
	if(records.empty())
	{
		setStatus(error.isEmpty() ? QString::fromUtf8("По запросу ничего не найдено.") : QString::fromUtf8("Ошибка поиска: %1").arg(error), true);
		return;
	}

	QDialog dialog(this);
	dialog.setWindowTitle(QString::fromUtf8("Результаты поиска объектов культуры"));
	dialog.resize(1000, 650);
	QVBoxLayout* layout = new QVBoxLayout(&dialog);
	QLabel* hint = new QLabel(QString::fromUtf8("Найдены записи только в реально подключённых источниках. Выберите запись и примените её к текущему объекту — для публичного изображения будет создана текстура на одностороннем Quad."), &dialog);
	hint->setWordWrap(true);
	layout->addWidget(hint);

	QHBoxLayout* body = new QHBoxLayout();
	QListWidget* list = new QListWidget(&dialog);
	list->setMinimumWidth(440);
	list->setSelectionMode(QAbstractItemView::SingleSelection);
	for(size_t i = 0; i < records.size(); ++i)
	{
		const CulturalApiRecord& record = records[i];
		QListWidgetItem* item = new QListWidgetItem(QStringLiteral("[%1] %2").arg(culturalApiProviderName(record.provider_id), record.display_text.isEmpty() ? record.title : record.display_text), list);
		item->setData(Qt::UserRole, (int)i);
		item->setToolTip(culturalApiDetails(record));
	}
	body->addWidget(list, 1);

	QVBoxLayout* details_layout = new QVBoxLayout();
	QLabel* preview = new QLabel(QString::fromUtf8("Предпросмотр недоступен"), &dialog);
	preview->setAlignment(Qt::AlignCenter);
	preview->setMinimumSize(300, 220);
	preview->setFrameShape(QFrame::StyledPanel);
	preview->setWordWrap(true);
	details_layout->addWidget(preview);
	QPlainTextEdit* details = new QPlainTextEdit(&dialog);
	details->setReadOnly(true);
	details_layout->addWidget(details, 1);
	body->addLayout(details_layout, 1);
	layout->addLayout(body, 1);

	QDialogButtonBox* buttons = new QDialogButtonBox(&dialog);
	QPushButton* apply = buttons->addButton(QString::fromUtf8("Применить к объекту"), QDialogButtonBox::AcceptRole);
	QPushButton* show_json = buttons->addButton(QString::fromUtf8("Показать JSON"), QDialogButtonBox::HelpRole);
	QPushButton* cancel = buttons->addButton(QDialogButtonBox::Cancel);
	layout->addWidget(buttons);

	auto update_details = [&records, list, preview, details]() {
		QListWidgetItem* item = list->currentItem();
		if(!item)
			return;
		const int index = item->data(Qt::UserRole).toInt();
		if(index < 0 || index >= (int)records.size())
			return;
		const CulturalApiRecord& record = records[(size_t)index];
		details->setPlainText(culturalApiDetails(record));
		const QPixmap pixmap = culturalApiPreview(record);
		if(pixmap.isNull())
		{
			preview->setPixmap(QPixmap());
			preview->setText(QString::fromUtf8("Предпросмотр не предоставлен источником"));
		}
		else
		{
			preview->setText(QString());
			preview->setPixmap(pixmap.scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
		}
	};
	connect(list, &QListWidget::currentRowChanged, &dialog, [update_details](int) mutable { update_details(); });
	connect(apply, &QPushButton::clicked, &dialog, [&dialog, list]() { if(list->currentItem()) dialog.accept(); });
	connect(show_json, &QPushButton::clicked, &dialog, [&records, list, &dialog]() {
		if(!list->currentItem())
			return;
		const int index = list->currentItem()->data(Qt::UserRole).toInt();
		if(index < 0 || index >= (int)records.size())
			return;
		QMessageBox box(&dialog);
		box.setWindowTitle(QString::fromUtf8("JSON источника"));
		box.setTextFormat(Qt::PlainText);
		box.setText(QString::fromUtf8(QJsonDocument(records[(size_t)index].raw).toJson(QJsonDocument::Indented)));
		box.setStandardButtons(QMessageBox::Ok);
		box.exec();
	});
	connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
	list->setCurrentRow(0);
	update_details();
	if(dialog.exec() != QDialog::Accepted || !list->currentItem())
	{
		setStatus(QString::fromUtf8("Выбор записи отменён."));
		return;
	}

	const int index = list->currentItem()->data(Qt::UserRole).toInt();
	if(index >= 0 && index < (int)records.size())
		importProviderRecord(records[(size_t)index]);
#endif
}


CulturalObjectSettings CulturalObjectEditor::settingsFromProviderRecord(const CulturalApiRecord& record)
{
	const QJsonObject& object = record.raw;
	const auto field = [&object](const QStringList& keys) { return culturalApiField(object, keys); };
	CulturalObjectSettings settings = CulturalObjectSettings::defaultObject();
	settings.uuid = stdString(QUuid::createUuid().toString(QUuid::WithoutBraces));
	settings.title = stdString(field({ QStringLiteral("title"), QStringLiteral("objectName") }));
	if(settings.title.empty())
		settings.title = stdString(record.title);
	settings.object_type = "museum_exhibit";
	settings.cultural_category = record.provider_id == QStringLiteral("artic") ? "artic_artwork" : "met_museum_object";
	settings.description = stdString(field({ QStringLiteral("description"), QStringLiteral("short_description"), QStringLiteral("objectDescription") }));
	settings.source_mode = "online";
	settings.provider_id = stdString(record.provider_id);
	settings.provider_record_id = stdString(record.record_id);
	settings.retrieval_status = "imported";
	settings.source_url = stdString(record.source_url);
	settings.raw_source_ref = stdString(record.source_url);
	settings.source_records_ref = stdString(record.provider_id + QStringLiteral(":") + record.record_id + QStringLiteral("|") + record.source_url);
	settings.media_cache_key = stdString(record.provider_id + QStringLiteral(":") + record.record_id);

	settings.alternative_titles = stdString(field({ QStringLiteral("alt_titles"), QStringLiteral("additionalImages") }));
	settings.creators = stdString(field({ QStringLiteral("artist_display"), QStringLiteral("artistDisplayName"), QStringLiteral("artist_title"), QStringLiteral("artistRole") }));
	settings.creation_date = stdString(field({ QStringLiteral("date_display"), QStringLiteral("objectDate") }));
	settings.country = stdString(field({ QStringLiteral("artistNationality"), QStringLiteral("country") }));
	settings.place_of_creation = stdString(field({ QStringLiteral("place_of_origin"), QStringLiteral("city"), QStringLiteral("region") }));
	settings.current_location = stdString(field({ QStringLiteral("repository_title"), QStringLiteral("repository"), QStringLiteral("gallery_title") }));
	settings.collection = stdString(field({ QStringLiteral("department_title"), QStringLiteral("department"), QStringLiteral("repository") }));
	settings.inventory_number = stdString(field({ QStringLiteral("main_reference_number"), QStringLiteral("accessionNumber"), QStringLiteral("id"), QStringLiteral("objectID") }));
	settings.art_forms = stdString(field({ QStringLiteral("artwork_type_title"), QStringLiteral("classification_title"), QStringLiteral("objectName") }));
	settings.museum_classifications = stdString(field({ QStringLiteral("classification_titles"), QStringLiteral("classification_title"), QStringLiteral("classification") }));
	settings.disciplines = stdString(field({ QStringLiteral("term_titles"), QStringLiteral("tags") }));
	settings.cultures = stdString(field({ QStringLiteral("culture"), QStringLiteral("artistCulture") }));
	settings.periods = stdString(field({ QStringLiteral("period"), QStringLiteral("dynasty"), QStringLiteral("reign") }));
	settings.materials = stdString(field({ QStringLiteral("medium_display"), QStringLiteral("medium"), QStringLiteral("material_titles") }));
	settings.techniques = stdString(field({ QStringLiteral("technique_titles") }));
	settings.styles = stdString(field({ QStringLiteral("style_titles"), QStringLiteral("style_title") }));
	settings.genres = stdString(field({ QStringLiteral("genre_titles") }));
	settings.subjects = stdString(field({ QStringLiteral("theme_titles"), QStringLiteral("subject_titles"), QStringLiteral("tags") }));
	settings.keywords = stdString(field({ QStringLiteral("term_titles"), QStringLiteral("tags") }));
	settings.iiif_id = stdString(field({ QStringLiteral("image_id") }));
	settings.museum_id = stdString(record.record_id);

	settings.card_title = settings.title;
	settings.card_subtitle = settings.creators;
	settings.card_summary = settings.description;
	settings.plaque_text = settings.title;
	settings.provenance = stdString(field({ QStringLiteral("provenance_text"), QStringLiteral("provenanceText") }));
	settings.exhibitions = stdString(field({ QStringLiteral("exhibition_history"), QStringLiteral("exhibitionHistory") }));
	settings.publications = stdString(field({ QStringLiteral("publication_history"), QStringLiteral("publicationHistory") }));
	settings.related_objects = stdString(field({ QStringLiteral("related_object_ids"), QStringLiteral("additionalImages") }));
	settings.rights_holder = stdString(field({ QStringLiteral("credit_line"), QStringLiteral("creditLine"), QStringLiteral("copyright_notice") }));
	settings.attribution_text = settings.rights_holder;
	settings.license_status = record.public_domain ? "free_use" : "unknown_license";
	settings.license_url = record.public_domain ? "https://creativecommons.org/publicdomain/zero/1.0/" : std::string();

	QString primary_image_source = record.preview_url;
	QString high_resolution_image;
	if(record.public_domain && record.provider_id == QStringLiteral("artic"))
	{
		const QString image_id = field({ QStringLiteral("image_id") });
		if(!image_id.isEmpty())
		{
			primary_image_source = QStringLiteral("https://www.artic.edu/iiif/2/%1/full/843,/0/default.jpg").arg(image_id);
			high_resolution_image = QStringLiteral("https://www.artic.edu/iiif/2/%1/full/1686,/0/default.jpg").arg(image_id);
		}
	}
	else if(record.public_domain && record.provider_id == QStringLiteral("met"))
		high_resolution_image = field({ QStringLiteral("primaryImage"), QStringLiteral("primaryImageSmall") });
	settings.primary_image_source_url = stdString(primary_image_source);
	settings.high_resolution_image_url = stdString(high_resolution_image);
	settings.iiif_manifest_url = stdString(record.iiif_manifest_url);
	settings.allow_display = record.public_domain && !primary_image_source.isEmpty();
	settings.allow_download = record.public_domain && !high_resolution_image.isEmpty();
	settings.allow_modify = record.public_domain && !high_resolution_image.isEmpty();
	settings.allow_commercial_use = record.public_domain && !high_resolution_image.isEmpty();
	settings.modified_at = stdString(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	return settings;
}


void CulturalObjectEditor::importProviderRecord(const CulturalApiRecord& record)
{
	CulturalApiRecord full_record = record;
	QString fetch_error;
	CulturalApiRecord fetched_record;
	if(CulturalApiClient::fetchRecord(record.provider_id, record.record_id, fetched_record, fetch_error))
		full_record = fetched_record;

	CulturalObjectSettings imported = settingsFromProviderRecord(full_record);
	if(!current_settings.uuid.empty())
		imported.uuid = current_settings.uuid;
	// The source image is kept separately until it has passed through the
	// resource cache.  A remote URL is never directly assigned as a world
	// material texture.
	imported.primary_image_url.clear();
	syncing = true;
	setControlsFromSettings(imported);
	syncing = false;

	if(imported.allow_display && !imported.primary_image_source_url.empty())
	{
		setStatus(QString::fromUtf8("Получены полные музейные данные. Кэширование публичного изображения в ресурсах мира…"));
		QApplication::setOverrideCursor(Qt::WaitCursor);
		emit publicImageImportRequested(qString(imported.primary_image_source_url));
		QApplication::restoreOverrideCursor();
	}
	else
	{
		setStatus(fetch_error.isEmpty()
			? QString::fromUtf8("Полные метаданные импортированы. Изображение не подключено: источник не подтвердил public domain или не выдал публичный файл.")
			: QString::fromUtf8("Метаданные импортированы из результата поиска. Полная запись временно недоступна: %1").arg(fetch_error),
			!fetch_error.isEmpty());
	}
	emit objectChanged();
	return;

#if 0 // Superseded by settingsFromProviderRecord(): it preserves the canonical source descriptor and imports the full record first.
	const QJsonObject& object = record.raw;
	const auto field = [&object](const QStringList& keys) { return culturalApiField(object, keys); };
	const auto set_field = [this, &field](const char* key, const QStringList& source_keys) {
		const QString value = field(source_keys);
		if(value.isEmpty())
			return;
		if(lines.count(key))
			lines[key]->setText(value);
		if(texts.count(key))
			texts[key]->setPlainText(value);
	};

	syncing = true;
	set_field("title", { QStringLiteral("title"), QStringLiteral("objectName") });
	set_field("description", { QStringLiteral("description"), QStringLiteral("objectDescription") });
	set_field("creators", { QStringLiteral("artist_display"), QStringLiteral("artistDisplayName"), QStringLiteral("artist_title") });
	set_field("creation_date", { QStringLiteral("date_display"), QStringLiteral("objectDate") });
	set_field("country", { QStringLiteral("culture") });
	set_field("place_of_creation", { QStringLiteral("place_of_origin"), QStringLiteral("city") });
	set_field("collection", { QStringLiteral("department"), QStringLiteral("repository") });
	set_field("inventory_number", { QStringLiteral("accessionNumber"), QStringLiteral("id"), QStringLiteral("objectID") });
	set_field("materials", { QStringLiteral("medium_display"), QStringLiteral("medium") });
	set_field("museum_classifications", { QStringLiteral("classification"), QStringLiteral("objectName") });
	set_field("provenance", { QStringLiteral("provenance_text"), QStringLiteral("provenanceText") });
	set_field("exhibitions", { QStringLiteral("exhibition_history"), QStringLiteral("exhibitionHistory") });
	set_field("publications", { QStringLiteral("publication_history"), QStringLiteral("publicationHistory") });
	set_field("rights_holder", { QStringLiteral("credit_line"), QStringLiteral("creditLine"), QStringLiteral("copyright_notice") });
	set_field("attribution_text", { QStringLiteral("credit_line"), QStringLiteral("creditLine"), QStringLiteral("copyright_notice") });
	lines["source_url"]->setText(record.source_url);

	const bool public_domain = object.value(QStringLiteral("is_public_domain")).toBool(false) || object.value(QStringLiteral("isPublicDomain")).toBool(false);
	QString primary_image;
	QString high_resolution_image;
	if(public_domain && record.provider_id == QStringLiteral("artic"))
	{
		const QString image_id = field({ QStringLiteral("image_id") });
		if(!image_id.isEmpty())
		{
			primary_image = QStringLiteral("https://www.artic.edu/iiif/2/%1/full/843,/0/default.jpg").arg(image_id);
			high_resolution_image = QStringLiteral("https://www.artic.edu/iiif/2/%1/full/1686,/0/default.jpg").arg(image_id);
			lines["iiif_manifest_url"]->setText(QStringLiteral("https://api.artic.edu/api/v1/artworks/%1/manifest.json").arg(record.record_id));
		}
	}
	else if(public_domain && record.provider_id == QStringLiteral("met"))
	{
		primary_image = field({ QStringLiteral("primaryImageSmall"), QStringLiteral("primaryImage") });
		high_resolution_image = field({ QStringLiteral("primaryImage") });
	}
	lines["primary_image_url"]->setText(primary_image);
	lines["high_resolution_image_url"]->setText(high_resolution_image);
	if(lines["card_title"]->text().trimmed().isEmpty())
		lines["card_title"]->setText(lines["title"]->text());
	if(texts["card_summary"]->toPlainText().trimmed().isEmpty())
		texts["card_summary"]->setPlainText(texts["description"]->toPlainText());
	combos["source_mode"]->setCurrentIndex(combos["source_mode"]->findData(QStringLiteral("online")));
	combos["provider_id"]->setCurrentIndex(combos["provider_id"]->findData(record.provider_id));
	lines["provider_record_id"]->setText(record.record_id);
	combos["license_status"]->setCurrentIndex(combos["license_status"]->findData(public_domain ? QStringLiteral("free_use") : QStringLiteral("unknown_license")));
	checks["allow_display"]->setChecked(public_domain && !primary_image.isEmpty());
	checks["allow_download"]->setChecked(public_domain && !high_resolution_image.isEmpty());
	checks["allow_modify"]->setChecked(public_domain && !high_resolution_image.isEmpty());
	checks["allow_commercial_use"]->setChecked(public_domain && !high_resolution_image.isEmpty());

	current_settings.provider_id = stdString(record.provider_id);
	current_settings.provider_record_id = stdString(record.record_id);
	current_settings.source_mode = "online";
	current_settings.retrieval_status = "imported";
	current_settings.source_url = stdString(record.source_url);
	current_settings.raw_source_ref = stdString(record.source_url);
	current_settings.source_records_ref = stdString(record.provider_id + QStringLiteral(":") + record.record_id + QStringLiteral("|") + record.source_url);
	current_settings.media_cache_key = stdString(record.provider_id + QStringLiteral(":") + record.record_id);
	QJsonObject preserved;
	preserved.insert(QStringLiteral("provider_id"), record.provider_id);
	preserved.insert(QStringLiteral("provider_record_id"), record.record_id);
	preserved.insert(QStringLiteral("title"), record.title);
	preserved.insert(QStringLiteral("source_url"), record.source_url);
	current_settings.preserved_json_fields = stdString(QString::fromUtf8(QJsonDocument(preserved).toJson(QJsonDocument::Compact)));
	syncing = false;

	if(public_domain && !primary_image.isEmpty())
	{
		setStatus(QString::fromUtf8("Кэширование публичного изображения в ресурсах мира…"));
		QApplication::setOverrideCursor(Qt::WaitCursor);
		emit publicImageImportRequested(primary_image);
		QApplication::restoreOverrideCursor();
	}
	else
		setStatus(QString::fromUtf8("Метаданные импортированы. Изображение не подключено: источник не подтвердил public domain для этой записи."));
	emit objectChanged();
#endif
}


void CulturalObjectEditor::importLocalJson()
{
	const QString path = line("local_file");
	QFile file(path);
	if(path.isEmpty() || !file.open(QIODevice::ReadOnly))
	{
		setStatus(QString::fromUtf8("Не удалось открыть локальный JSON."), true);
		return;
	}

	QJsonParseError parse_error;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !document.isObject())
	{
		setStatus(QString::fromUtf8("Ошибка JSON: %1").arg(parse_error.errorString()), true);
		return;
	}

	const QJsonObject object = document.object();
	const auto valueAsText = [](const QJsonValue& value) -> QString {
		if(value.isString())
			return value.toString();
		if(value.isDouble())
			return QString::number(value.toDouble(), 'g', 15);
		if(value.isArray())
		{
			QStringList values;
			for(const QJsonValue& element : value.toArray())
			{
				if(element.isString())
					values.push_back(element.toString());
				else if(element.isObject())
				{
					const QJsonObject element_object = element.toObject();
					const QString display = element_object.value(QStringLiteral("name")).toString(
						element_object.value(QStringLiteral("value")).toString());
					if(!display.isEmpty())
						values.push_back(display);
				}
			}
			return values.join(QStringLiteral("; "));
		}
		return QString();
	};

	syncing = true;
	const auto setField = [this, &object, &valueAsText](const char* field, const char* source) {
		const QString value = valueAsText(object.value(QString::fromLatin1(source)));
		if(value.isEmpty())
			return;
		const auto line_it = lines.find(field);
		if(line_it != lines.end())
			line_it->second->setText(value);
		const auto text_it = texts.find(field);
		if(text_it != texts.end())
			text_it->second->setPlainText(value);
	};
	setField("title", "title");
	if(line("title").isEmpty())
		setField("title", "name");
	setField("description", "description");
	setField("creators", "creators");
	if(text("creators").isEmpty())
		setField("creators", "creator");
	setField("creation_date", "creation_date");
	if(line("creation_date").isEmpty())
		setField("creation_date", "date");
	setField("materials", "materials");
	setField("techniques", "techniques");
	if(line("techniques").isEmpty())
		setField("techniques", "technique");
	setField("primary_image_url", "image");
	setField("iiif_manifest_url", "iiif_manifest");
	setField("license_url", "license_url");

	current_settings.raw_source_ref = stdString(path);
	current_settings.source_mode = "local";
	combos["source_mode"]->setCurrentIndex(combos["source_mode"]->findData(QStringLiteral("local")));
	syncing = false;
	setStatus(QString::fromUtf8("Локальный JSON импортирован. Исходный файл сохранён как ссылка, а не встроен в content."));
	emit objectChanged();
}


void CulturalObjectEditor::showRawJson()
{
	QMessageBox box(this);box.setWindowTitle(QString::fromUtf8("CulturalObject JSON"));box.setTextFormat(Qt::PlainText);box.setText(qString(CulturalObjectSettings::serialiseToContent(controlsToSettings())));box.setStandardButtons(QMessageBox::Ok);box.exec();
}
