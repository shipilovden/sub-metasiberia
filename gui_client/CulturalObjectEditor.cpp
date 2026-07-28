/*=====================================================================
CulturalObjectEditor.cpp
------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "CulturalObjectEditor.h"


#include "../qt/QtUtils.h"
#include "../utils/Exception.h"
#include <maths/matrix3.h>
#include <maths/mathstypes.h>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStringList>
#include <QtCore/QUuid>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>


namespace
{
static std::string stdString(const QString& s) { const QByteArray b = s.toUtf8(); return std::string(b.constData(), (size_t)b.size()); }
static QString qString(const std::string& s) { return QString::fromUtf8(s.data(), (int)s.size()); }
}


CulturalObjectEditor::CulturalObjectEditor(QWidget* parent)
:	QWidget(parent),
	editing_ob_uid(UID(0)),
	controls_editable(false),
	syncing(false),
	show_3d_controls(NULL), snap_to_grid(NULL), grid_spacing(NULL),
	pos_x(NULL), pos_y(NULL), pos_z(NULL), scale_x(NULL), scale_y(NULL), scale_z(NULL), rot_x(NULL), rot_y(NULL), rot_z(NULL)
{
	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(7);
	QLabel* heading = new QLabel(QString::fromUtf8("<b>MetaSiberia. Редактор объектов культуры</b>"), this);
	heading->setTextFormat(Qt::RichText);
	root->addWidget(heading);
	info_label = new QLabel(QString::fromUtf8("CulturalObject: новый объект"), this);
	info_label->setWordWrap(true);
	info_label->setFrameShape(QFrame::StyledPanel);
	root->addWidget(info_label);
	status_label = new QLabel(QString::fromUtf8("Локальный MVP: отдельная модель данных, импорт JSON и шесть вкладок."), this);
	status_label->setWordWrap(true);
	root->addWidget(status_label);

	tabs = new QTabWidget(this);
	tabs->setUsesScrollButtons(true);
	root->addWidget(tabs, 1);

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
	addCombo(g, row, "provider_id", QString::fromUtf8("Онлайн-база"), { QString::fromUtf8("Вручную|manual"), QStringLiteral("Art Institute of Chicago|artic"), QStringLiteral("The Met|met"), QStringLiteral("Smithsonian|smithsonian"), QStringLiteral("Europeana|europeana"), QStringLiteral("Wikidata|wikidata"), QStringLiteral("IIIF|iiif") });
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
	connect(search, &QPushButton::clicked, this, [this]() { setStatus(QString::fromUtf8("Слой Cultural API подготовлен архитектурно; онлайн-провайдеры будут подключены следующим этапом.")); });
	connect(refresh, &QPushButton::clicked, this, [this]() { setStatus(QString::fromUtf8("Обновление не перезаписывает пользовательские поля. Для текущего локального объекта внешняя запись не выбрана.")); });
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
	addLine(g,row,"model_3d_url",QString::fromUtf8("3D-модель")); addLine(g,row,"audio_url",QString::fromUtf8("Аудио / аудиогид")); addLine(g,row,"video_url",QString::fromUtf8("Видео")); addText(g,row,"documents",QString::fromUtf8("Документы")); addLine(g,row,"media_cache_key",QString::fromUtf8("Ключ кэша")); addCheck(g,row,"lazy_media_loading",QString::fromUtf8("Ленивая загрузка"));
	addCombo(g,row,"license_status",QString::fromUtf8("Статус лицензии"),{QStringLiteral("free_use|free_use"),QStringLiteral("attribution_required|attribution_required"),QStringLiteral("non_commercial|non_commercial"),QStringLiteral("restricted|restricted"),QStringLiteral("metadata_only|metadata_only"),QStringLiteral("unknown_license|unknown_license"),QStringLiteral("blocked|blocked")});
	addLine(g,row,"rights_holder",QString::fromUtf8("Правообладатель")); addLine(g,row,"license_url",QString::fromUtf8("URL лицензии")); addText(g,row,"attribution_text",QString::fromUtf8("Атрибуция"));
	addCheck(g,row,"allow_display",QString::fromUtf8("Разрешено отображение")); addCheck(g,row,"allow_download",QString::fromUtf8("Разрешено скачивание")); addCheck(g,row,"allow_modify",QString::fromUtf8("Разрешено изменение")); addCheck(g,row,"allow_commercial_use",QString::fromUtf8("Разрешено коммерческое использование"));
	Q_UNUSED(media_tab);

	QWidget* history_tab = makeTab(QString::fromUtf8("История"), &g); row = 0;
	addText(g,row,"provenance",QStringLiteral("Provenance / владельцы"),130); addText(g,row,"exhibitions",QString::fromUtf8("Выставки"),100); addText(g,row,"restorations",QString::fromUtf8("Реставрации"),100); addText(g,row,"condition",QString::fromUtf8("Состояние")); addText(g,row,"publications",QString::fromUtf8("Публикации"),100); addText(g,row,"related_objects",QString::fromUtf8("Связанные объекты"));
	Q_UNUSED(history_tab);

	QWidget* exhibition_tab = makeTab(QString::fromUtf8("Экспозиция"), &g); row = 0;
	show_3d_controls = new QCheckBox(QString::fromUtf8("Показывать 3D-контролы позиции/поворота"), exhibition_tab); g->addWidget(show_3d_controls,row++,0,1,2);
	addLine(g,row,"exhibition_scene",QString::fromUtf8("Сцена")); addLine(g,row,"exhibition_room",QString::fromUtf8("Зал")); addLine(g,row,"exhibition_zone",QString::fromUtf8("Зона"));
	QLabel* axes = new QLabel(QStringLiteral("X / Y / Z"), exhibition_tab); g->addWidget(axes,row,1); row++;
	pos_x=addDouble(g,row,"_pos_x",QString::fromUtf8("Позиция X"),-1e9,1e9,0.05,4); pos_y=addDouble(g,row,"_pos_y",QString::fromUtf8("Позиция Y"),-1e9,1e9,0.05,4); pos_z=addDouble(g,row,"_pos_z",QString::fromUtf8("Позиция Z"),-1e9,1e9,0.05,4);
	rot_x=addDouble(g,row,"_rot_x",QString::fromUtf8("Поворот X"),-360000,360000,1.0,2); rot_y=addDouble(g,row,"_rot_y",QString::fromUtf8("Поворот Y"),-360000,360000,1.0,2); rot_z=addDouble(g,row,"_rot_z",QString::fromUtf8("Поворот Z"),-360000,360000,1.0,2);
	scale_x=addDouble(g,row,"_scale_x",QString::fromUtf8("Масштаб X"),0.0001,1e6,0.05,4); scale_y=addDouble(g,row,"_scale_y",QString::fromUtf8("Масштаб Y"),0.0001,1e6,0.05,4); scale_z=addDouble(g,row,"_scale_z",QString::fromUtf8("Масштаб Z"),0.0001,1e6,0.05,4);
	snap_to_grid = new QCheckBox(QString::fromUtf8("Привязка к сетке"), exhibition_tab); g->addWidget(snap_to_grid,row,0); grid_spacing = new QDoubleSpinBox(exhibition_tab); grid_spacing->setRange(0.001,1000000); grid_spacing->setValue(1.0); grid_spacing->setSuffix(QStringLiteral(" m")); g->addWidget(grid_spacing,row++,1);
	addCombo(g,row,"placement",QString::fromUtf8("Размещение"),{QString::fromUtf8("Свободно|free"),QString::fromUtf8("На стене|wall"),QString::fromUtf8("На подставке|pedestal"),QString::fromUtf8("В витрине|case")});
	addLine(g,row,"frame_style",QString::fromUtf8("Рама")); addLine(g,row,"pedestal_style",QString::fromUtf8("Подставка")); addLine(g,row,"case_style",QString::fromUtf8("Витрина")); addCheck(g,row,"spotlight",QString::fromUtf8("Прожектор")); addDouble(g,row,"light_intensity",QString::fromUtf8("Интенсивность света"),0,100,0.05,2); addCheck(g,row,"shadows",QString::fromUtf8("Тени")); addCheck(g,row,"interactive",QString::fromUtf8("Интерактивность")); addDouble(g,row,"activation_distance",QString::fromUtf8("Дистанция активации"),0,10000,0.25,2);
	addLine(g,row,"route_id",QString::fromUtf8("Маршрут")); addLine(g,row,"route_stop",QString::fromUtf8("Номер остановки")); addLine(g,row,"next_object_uuid",QString::fromUtf8("Следующий объект UUID")); addText(g,row,"curator_note",QString::fromUtf8("Кураторская заметка"),100);

	connectChangeSignals();
	connect(show_3d_controls, &QCheckBox::toggled, this, [this](bool) { if(!syncing) emit posAndRot3DControlsToggled(); });
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
void CulturalObjectEditor::setControlsEditable(bool editable){controls_editable=editable;setEnabled(true);for(auto&p:lines)if(p.first!="uuid")p.second->setReadOnly(!editable);for(auto&p:texts)p.second->setReadOnly(!editable);for(auto&p:combos)p.second->setEnabled(editable);for(auto&p:checks)p.second->setEnabled(editable);for(auto&p:doubles)p.second->setEnabled(editable);show_3d_controls->setEnabled(true);}
bool CulturalObjectEditor::posAndRot3DControlsEnabled()const{return show_3d_controls&&show_3d_controls->isChecked();}
void CulturalObjectEditor::setPosAndRot3DControlsEnabled(bool enabled){if(show_3d_controls){const QSignalBlocker blocker(show_3d_controls);show_3d_controls->setChecked(enabled);}}
bool CulturalObjectEditor::snapToGridChecked()const{return snap_to_grid&&snap_to_grid->isChecked();}
double CulturalObjectEditor::gridSpacing()const{return grid_spacing?grid_spacing->value():1.0;}


void CulturalObjectEditor::setStatus(const QString& s,bool error){status_label->setText(s);QPalette p=status_label->palette();p.setColor(QPalette::WindowText,error?QColor(QStringLiteral("#FF6B6B")):palette().color(QPalette::WindowText));status_label->setPalette(p);}


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
