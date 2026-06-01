/*=====================================================================
BotEditorWidget.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "BotEditorWidget.h"
#include "GUIClient.h"
#include "../shared/Avatar.h"
#include <maths/vec3.h>

#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QFileDialog>
#include <QtCore/QTimer>
#include <QtCore/QSignalBlocker>
#include <QtCore/QCoreApplication>


static QDoubleSpinBox* makeSpin(double val, double lo, double hi, double step=0.1, int dec=2)
{
	auto* s = new QDoubleSpinBox();
	s->setRange(lo, hi);
	s->setValue(val);
	s->setSingleStep(step);
	s->setDecimals(dec);
	return s;
}

static const uint32 BOT_DISABLED_FLAG = 1;
static const uint32 BOT_ALWAYS_FACE_FLAG = 2;
static const uint32 BOT_STATIONARY_FLAG = 4;
static const uint32 BOT_AUDIO_LOOP_FLAG = 8;
static const uint32 BOT_AUDIO_SPATIAL_FLAG = 16;
static const uint32 BOT_AUDIO_AUTOPLAY_FLAG = 32;

static const uint32 BOT_TRIGGER_PROXIMITY_FLAG = 1;
static const uint32 BOT_TRIGGER_CHAT_FLAG = 2;
static const uint32 BOT_TRIGGER_KEYWORDS_FLAG = 4;
static const uint32 BOT_TRIGGER_GESTURE_FLAG = 8;
static const uint32 BOT_TRIGGER_USE_FLAG = 16;


BotEditorWidget::BotEditorWidget(QWidget* parent)
:	QWidget(parent)
{
	move_timer = new QTimer(this);
	move_timer->setSingleShot(true);
	move_timer->setInterval(150); // 150 ms debounce
	connect(move_timer, &QTimer::timeout, this, &BotEditorWidget::sendMoveBot);

	buildUI();
	clear();
}

BotEditorWidget::~BotEditorWidget() {}

void BotEditorWidget::init(GUIClient* gc) { gui_client = gc; }


void BotEditorWidget::buildUI()
{
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	auto* list_box = new QGroupBox("Список ботов", this);
	auto* list_layout = new QVBoxLayout(list_box);
	list_layout->setContentsMargins(4, 4, 4, 4);
	bot_list_widget = new QListWidget(list_box);
	bot_list_widget->setMaximumHeight(110);
	list_layout->addWidget(bot_list_widget);
	refresh_bots_btn = new QPushButton("Обновить список", list_box);
	list_layout->addWidget(refresh_bots_btn);
	connect(bot_list_widget, &QListWidget::currentRowChanged, this, &BotEditorWidget::onBotListCurrentRowChanged);
	connect(refresh_bots_btn, &QPushButton::clicked, this, &BotEditorWidget::onRefreshBots);
	root->addWidget(list_box);

	// ── Position / Transform (always visible) ───────────────────────
	auto* pos_box = new QGroupBox("Позиция и поворот", this);
	auto* pos_form = new QFormLayout(pos_box);
	pos_form->setSpacing(3);

	pos_x_spin   = makeSpin(0, -100000, 100000, 0.1, 2);
	pos_y_spin   = makeSpin(0, -100000, 100000, 0.1, 2);
	pos_z_spin   = makeSpin(1.70, -1000, 10000, 0.1, 2);
	heading_spin = makeSpin(0, -180, 180, 1, 1);

	pos_form->addRow("X:",          pos_x_spin);
	pos_form->addRow("Y:",          pos_y_spin);
	pos_form->addRow("Z (высота):", pos_z_spin);
	pos_form->addRow("Поворот (°):", heading_spin);
	root->addWidget(pos_box);

	// live position update with debounce
	auto schd = [this](double){ scheduleMove(); };
	connect(pos_x_spin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schd);
	connect(pos_y_spin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schd);
	connect(pos_z_spin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schd);
	connect(heading_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schd);

	// ── Tabs ────────────────────────────────────────────────────────
	tab_widget = new QTabWidget(this);

	// ─────────── Identity tab ───────────────────────────────────────
	{
		auto* tab = new QWidget();
		auto* fl  = new QFormLayout(tab);
		fl->setSpacing(4);

		name_edit = new QLineEdit(tab);
		name_edit->setPlaceholderText("Имя бота");
		fl->addRow("Имя:", name_edit);
		// Instant name + avatar update on Enter/Tab
		connect(name_edit, &QLineEdit::editingFinished, this, &BotEditorWidget::sendUpdateBot);

		auto* url_row = new QHBoxLayout();
		avatar_url_edit = new QLineEdit(tab);
		avatar_url_edit->setPlaceholderText("URL модели (.glb/.bmesh) или локальный путь");
		avatar_browse = new QPushButton("...", tab);
		avatar_browse->setFixedWidth(32);
		connect(avatar_browse, &QPushButton::clicked, this, &BotEditorWidget::onBrowseAvatar);
		connect(avatar_url_edit, &QLineEdit::editingFinished, this, &BotEditorWidget::onAvatarURLChanged);
		url_row->addWidget(avatar_url_edit);
		url_row->addWidget(avatar_browse);
		fl->addRow("Аватар:", url_row);

		fl->addRow(new QLabel("<b>Масштаб</b>", tab));
		scale_x_spin = makeSpin(1.0, 0.01, 10.0, 0.05, 2);
		scale_y_spin = makeSpin(1.0, 0.01, 10.0, 0.05, 2);
		scale_z_spin = makeSpin(1.0, 0.01, 10.0, 0.05, 2);
		fl->addRow("Scale X:", scale_x_spin);
		fl->addRow("Scale Y:", scale_y_spin);
		fl->addRow("Scale Z:", scale_z_spin);

		tab_widget->addTab(tab, "Личность");
	}

	// ─────────── AI / Prompt tab ────────────────────────────────────
	{
		auto* tab = new QWidget();
		auto* vl  = new QVBoxLayout(tab);
		vl->setSpacing(4);

		vl->addWidget(new QLabel("Системный промпт (характер, инструкции):", tab));
		prompt_edit = new QPlainTextEdit(tab);
		prompt_edit->setPlaceholderText(
			"Ты полезный ИИ-ассистент в виртуальном мире Метасибирь.\n"
			"Ты находишься в определённом месте и общаешься с посетителями.\n"
			"Отвечай кратко и по делу.");
		prompt_edit->setMinimumHeight(120);
		vl->addWidget(prompt_edit);

		auto* ai_form = new QFormLayout();
		ai_form->setSpacing(4);
		ai_model_edit = new QLineEdit(tab);
		ai_model_edit->setPlaceholderText("openai/gpt-4o-mini, xai/grok-4 ... empty = server default");
		ai_preset_edit = new QLineEdit(tab);
		ai_preset_edit->setPlaceholderText("guide, shopkeeper, quest giver, guard, companion");
		ai_temperature_spin = makeSpin(0.7, 0.0, 2.0, 0.05, 2);
		ai_max_tokens_spin = new QSpinBox(tab);
		ai_max_tokens_spin->setRange(0, 32000);
		ai_max_tokens_spin->setSingleStep(128);
		ai_max_tokens_spin->setSpecialValueText("default");
		ai_knowledge_edit = new QPlainTextEdit(tab);
		ai_knowledge_edit->setPlaceholderText("Bot facts, local lore, quest rules, shop inventory, forbidden topics...");
		ai_knowledge_edit->setMinimumHeight(70);
		ai_form->addRow("AI model:", ai_model_edit);
		ai_form->addRow("Personality:", ai_preset_edit);
		ai_form->addRow("Temperature:", ai_temperature_spin);
		ai_form->addRow("Max tokens:", ai_max_tokens_spin);
		ai_form->addRow("Knowledge:", ai_knowledge_edit);
		fallback_msg_edit = new QLineEdit(tab);
		fallback_msg_edit->setPlaceholderText("Сообщение когда LLM недоступен (пусто — молчать)");
		ai_form->addRow("Фолбэк-ответ:", fallback_msg_edit);

		api_key_edit = new QLineEdit(tab);
		api_key_edit->setPlaceholderText("API ключ для этого бота (пусто = сервер по умолчанию)");
		api_key_edit->setEchoMode(QLineEdit::Password);
		api_endpoint_edit = new QLineEdit(tab);
		api_endpoint_edit->setPlaceholderText("Endpoint (пусто = по умолчанию провайдера)");
		ai_form->addRow("API ключ:", api_key_edit);
		ai_form->addRow("Endpoint:", api_endpoint_edit);
		vl->addLayout(ai_form);

		llm_note = new QLabel(
			"<i>Провайдер LLM и API-ключ — в настройках сервера\n"
			"(substrata_server_config.xml).</i>", tab);
		llm_note->setWordWrap(true);
		vl->addWidget(llm_note);
		vl->addStretch();

		tab_widget->addTab(tab, "ИИ / Промпт");
	}

	// ─────────── Behaviour tab ──────────────────────────────────────
	{
		auto* tab = new QWidget();
		auto* fl  = new QFormLayout(tab);
		fl->setSpacing(4);

		fl->addRow(new QLabel("<b>Расстояния реакций</b>", tab));
		greet_dist_spin    = makeSpin(6.0,  0.5, 100, 0.5, 1);
		farewell_dist_spin = makeSpin(10.0, 0.5, 100, 0.5, 1);
		talk_radius_spin   = makeSpin(8.0,  0.5, 100, 0.5, 1);
		fl->addRow("Приветствие (м):",       greet_dist_spin);
		fl->addRow("Прощание (м):",          farewell_dist_spin);
		fl->addRow("Слышит чат в радиусе (м):", talk_radius_spin);

		fl->addRow(new QLabel("<b>Поведение</b>", tab));
		always_face_cb = new QCheckBox("Всегда смотреть на ближайшего пользователя", tab);
		stationary_cb  = new QCheckBox("Стационарный (не уходить со спавна)", tab);
		stationary_cb->setChecked(true);
		disabled_cb    = new QCheckBox("Отключить бота (пауза)", tab);
		fl->addRow("", always_face_cb);
		fl->addRow("", stationary_cb);
		fl->addRow("", disabled_cb);
		fl->addRow(new QLabel("<b>Triggers</b>", tab));
		trigger_proximity_cb = new QCheckBox("React when a player enters/leaves radius", tab);
		trigger_chat_cb = new QCheckBox("React to nearby chat", tab);
		trigger_keywords_cb = new QCheckBox("React to keywords", tab);
		trigger_gesture_cb = new QCheckBox("React to player gestures", tab);
		trigger_use_cb = new QCheckBox("React to Use / E interaction", tab);
		trigger_keywords_edit = new QLineEdit(tab);
		trigger_keywords_edit->setPlaceholderText("comma-separated keywords: quest, help, shop");
		trigger_cooldown_spin = makeSpin(3.0, 0.0, 3600.0, 1.0, 1);
		fl->addRow("", trigger_proximity_cb);
		fl->addRow("", trigger_chat_cb);
		fl->addRow("", trigger_keywords_cb);
		fl->addRow("", trigger_gesture_cb);
		fl->addRow("", trigger_use_cb);
		fl->addRow("Keywords:", trigger_keywords_edit);
		fl->addRow("Trigger cooldown (s):", trigger_cooldown_spin);

		fl->addRow(new QLabel("<b>Действие при нажатии E</b>", tab));
		use_action_combo = new QComboBox(tab);
		use_action_combo->addItem("Ответ через LLM");          // 0 = USE_ACTION_LLM
		use_action_combo->addItem("Сказать текст");              // 1 = USE_ACTION_SAY_TEXT
		use_action_combo->addItem("Воспроизвести жест");         // 2 = USE_ACTION_GESTURE
		use_action_combo->addItem("Ничего не делать");           // 3 = USE_ACTION_NONE
		use_action_param_edit = new QLineEdit(tab);
		use_action_param_edit->setPlaceholderText("Текст / название жеста (greeting, idle, reactive, surprise, acknowledge)");
		fl->addRow("Тип действия:", use_action_combo);
		fl->addRow("Параметр:", use_action_param_edit);

		tab_widget->addTab(tab, "Поведение");
	}

	// ─────────── Animations tab ─────────────────────────────────────
	{
		auto* tab = new QWidget();
		auto* fl  = new QFormLayout(tab);
		fl->setSpacing(3);

		auto section = [&](const char* title){ fl->addRow(new QLabel(QString("<b>%1</b>").arg(title), tab)); };
		auto le = [&](const char* hint) -> QLineEdit* {
			auto* e = new QLineEdit(tab); e->setPlaceholderText(hint); return e;
		};

		auto flags_row = [&](QCheckBox*& loop_cb, QCheckBox*& head_cb){
			auto* row = new QHBoxLayout();
			loop_cb = new QCheckBox("Loop", tab);
			head_cb = new QCheckBox("Animate head", tab);
			row->addWidget(loop_cb);
			row->addWidget(head_cb);
			row->addStretch();
			fl->addRow("Flags:", row);
		};

		section("Приветствие (при приближении)");
		greet_name_edit = le("e.g. wave");    fl->addRow("Анимация:", greet_name_edit);
		greet_url_edit  = le("URL .subanim"); fl->addRow("URL:", greet_url_edit);
		greet_cd = makeSpin(30, 0, 3600, 5, 0); fl->addRow("Пауза (с):", greet_cd);
		flags_row(greet_loop_cb, greet_head_cb);
		greet_test_btn = new QPushButton("Test greeting", tab);
		connect(greet_test_btn, &QPushButton::clicked, this, [this]{ sendUpdateBot(); if(gui_client && bot_id != 0) gui_client->testBotGesture(bot_id, 0); });
		fl->addRow("", greet_test_btn);

		section("Ожидание (периодический жест)");
		idle_name_edit = le("e.g. idle");    fl->addRow("Анимация:", idle_name_edit);
		idle_url_edit  = le("URL .subanim"); fl->addRow("URL:", idle_url_edit);
		idle_int = makeSpin(20, 0, 3600, 5, 0); fl->addRow("Интервал (с):", idle_int);
		flags_row(idle_loop_cb, idle_head_cb);
		idle_test_btn = new QPushButton("Test idle", tab);
		connect(idle_test_btn, &QPushButton::clicked, this, [this]{ sendUpdateBot(); if(gui_client && bot_id != 0) gui_client->testBotGesture(bot_id, 1); });
		fl->addRow("", idle_test_btn);

		section("Реактивный (реакция на жест/чат)");
		react_name_edit = le("e.g. nod");    fl->addRow("Анимация:", react_name_edit);
		react_url_edit  = le("URL .subanim"); fl->addRow("URL:", react_url_edit);
		react_cd2 = makeSpin(15, 0, 3600, 5, 0); fl->addRow("Пауза (с):", react_cd2);
		flags_row(react_loop_cb, react_head_cb);
		react_test_btn = new QPushButton("Test reactive", tab);
		connect(react_test_btn, &QPushButton::clicked, this, [this]{ sendUpdateBot(); if(gui_client && bot_id != 0) gui_client->testBotGesture(bot_id, 2); });
		fl->addRow("", react_test_btn);

		section("Удивление (ручной слот)");
		surprise_name_edit = le("e.g. Surprised");  fl->addRow("Анимация:", surprise_name_edit);
		surprise_url_edit  = le("URL .subanim");     fl->addRow("URL:", surprise_url_edit);
		surprise_cd = makeSpin(15, 0, 3600, 5, 0);  fl->addRow("Пауза (с):", surprise_cd);
		flags_row(surprise_loop_cb, surprise_head_cb);
		surprise_test_btn = new QPushButton("Test surprise", tab);
		connect(surprise_test_btn, &QPushButton::clicked, this, [this]{ sendUpdateBot(); if(gui_client && bot_id != 0) gui_client->testBotGesture(bot_id, 3); });
		fl->addRow("", surprise_test_btn);

		section("Подтверждение / кивок (ручной слот)");
		acknowledge_name_edit = le("e.g. Nod"); fl->addRow("Анимация:", acknowledge_name_edit);
		acknowledge_url_edit  = le("URL .subanim"); fl->addRow("URL:", acknowledge_url_edit);
		acknowledge_cd = makeSpin(10, 0, 3600, 5, 0); fl->addRow("Пауза (с):", acknowledge_cd);
		flags_row(acknowledge_loop_cb, acknowledge_head_cb);
		acknowledge_test_btn = new QPushButton("Test acknowledge", tab);
		connect(acknowledge_test_btn, &QPushButton::clicked, this, [this]{ sendUpdateBot(); if(gui_client && bot_id != 0) gui_client->testBotGesture(bot_id, 4); });
		fl->addRow("", acknowledge_test_btn);

		tab_widget->addTab(tab, "Анимации");
	}

	// ─────────── Audio tab ──────────────────────────────────────────
	{
		auto* tab = new QWidget();
		auto* fl  = new QFormLayout(tab);
		fl->setSpacing(4);

		fl->addRow(new QLabel(
			"<i>Бот может транслировать звук из URL-источника\n"
			"(фоновая музыка, голос, объявления).</i>", tab));

		auto* url_row = new QHBoxLayout();
		audio_url_edit = new QLineEdit(tab);
		audio_url_edit->setPlaceholderText("https://... или subaudio:// ресурс");
		audio_browse = new QPushButton("...", tab);
		audio_browse->setFixedWidth(32);
		connect(audio_browse, &QPushButton::clicked, this, [this]{
			QString p = QFileDialog::getOpenFileName(this, "Выбрать аудио", {},
				"Аудио (*.mp3 *.ogg *.wav *.flac)");
			if(!p.isEmpty())
			{
				std::string url = p.toStdString();
				if(gui_client) url = gui_client->uploadLocalFileForBot(url);
				audio_url_edit->setText(QString::fromStdString(url));
			}
		});
		connect(audio_url_edit, &QLineEdit::editingFinished, this, &BotEditorWidget::sendUpdateBot);
		url_row->addWidget(audio_url_edit);
		url_row->addWidget(audio_browse);
		fl->addRow("Аудио URL:", url_row);

		audio_vol_spin    = makeSpin(1.0, 0.0, 1.0, 0.05, 2);
		audio_radius_spin = makeSpin(10.0, 0.5, 200, 1.0, 1);
		audio_activation_spin = makeSpin(12.0, 0.5, 500, 1.0, 1);
		audio_cooldown_spin = makeSpin(0.0, 0.0, 3600, 1.0, 1);
		fl->addRow("Activation radius (m):", audio_activation_spin);
		fl->addRow("Restart cooldown (s):", audio_cooldown_spin);
		audio_autoplay_cb = new QCheckBox("Autoplay when player enters radius", tab);
		audio_autoplay_cb->setChecked(true);
		fl->addRow("Громкость:", audio_vol_spin);
		fl->addRow("Радиус слышимости (м):", audio_radius_spin);

		audio_loop_cb    = new QCheckBox("Зациклить воспроизведение", tab);
		audio_loop_cb->setChecked(true);
		audio_spatial_cb = new QCheckBox("Пространственный звук (3D)", tab);
		audio_spatial_cb->setChecked(true);
		fl->addRow("", audio_autoplay_cb);
		fl->addRow("", audio_loop_cb);
		fl->addRow("", audio_spatial_cb);

		tab_widget->addTab(tab, "Звук");
	}

	root->addWidget(tab_widget);
	root->addStretch(1);

	// ── Bottom buttons ───────────────────────────────────────────────
	auto* btn_row = new QHBoxLayout();
	delete_btn = new QPushButton("Удалить бота", this);
	cancel_btn = new QPushButton("Отмена", this);
	save_btn   = new QPushButton("Сохранить", this);
	save_btn->setDefault(true);
	btn_row->addWidget(delete_btn);
	btn_row->addStretch();
	btn_row->addWidget(cancel_btn);
	btn_row->addWidget(save_btn);
	root->addLayout(btn_row);

	connect(save_btn,   &QPushButton::clicked, this, &BotEditorWidget::onSave);
	connect(cancel_btn, &QPushButton::clicked, this, &BotEditorWidget::onCancel);
	connect(delete_btn, &QPushButton::clicked, this, &BotEditorWidget::onDelete);
}


void BotEditorWidget::setBot(uint64_t bot_id_, const UID& avatar_uid_,
	const std::string& name, const std::string& avatar_url,
	const std::string& prompt,
	double px, double py, double pz, double heading_deg,
	const std::string& greeting_name, const std::string& greeting_url, double greeting_cooldown,
	const std::string& idle_name, const std::string& idle_url, double idle_interval,
	const std::string& reactive_name, const std::string& reactive_url, double reactive_cooldown,
	uint32 flags, double greeting_distance, double farewell_distance, double chat_radius,
	const Vec3f& model_scale,
	const std::string& ai_model_id, const std::string& ai_personality_preset, const std::string& ai_knowledge, double ai_temperature, uint32 ai_max_tokens,
	const std::string& audio_url, double audio_volume, double audio_radius, double audio_activation_distance, double audio_cooldown,
	uint32 trigger_flags, const std::string& trigger_keywords, double trigger_cooldown,
	uint32 greeting_gesture_flags, uint32 idle_gesture_flags, uint32 reactive_gesture_flags,
	const std::string& fallback_message,
	const std::string& surprise_name, const std::string& surprise_url, uint32 surprise_flags, double surprise_cooldown,
	const std::string& acknowledge_name, const std::string& acknowledge_url, uint32 acknowledge_flags, double acknowledge_cooldown,
	uint32 use_action_type, const std::string& use_action_param,
	const std::string& api_key, const std::string& api_endpoint)
{
	bot_id     = bot_id_;
	avatar_uid = avatar_uid_;

	QSignalBlocker bx(pos_x_spin), by(pos_y_spin), bz(pos_z_spin), bh(heading_spin);
	pos_x_spin->setValue(px);
	pos_y_spin->setValue(py);
	pos_z_spin->setValue(pz);
	heading_spin->setValue(heading_deg);

	name_edit->setText(QString::fromStdString(name));
	avatar_url_edit->setText(QString::fromStdString(avatar_url));
	scale_x_spin->setValue(model_scale.x);
	scale_y_spin->setValue(model_scale.y);
	scale_z_spin->setValue(model_scale.z);
	prompt_edit->setPlainText(QString::fromStdString(prompt));
	ai_model_edit->setText(QString::fromStdString(ai_model_id));
	ai_preset_edit->setText(QString::fromStdString(ai_personality_preset));
	ai_knowledge_edit->setPlainText(QString::fromStdString(ai_knowledge));
	ai_temperature_spin->setValue(ai_temperature);
	ai_max_tokens_spin->setValue((int)ai_max_tokens);
	greet_name_edit->setText(QString::fromStdString(greeting_name));
	greet_url_edit->setText(QString::fromStdString(greeting_url));
	greet_cd->setValue(greeting_cooldown);
	idle_name_edit->setText(QString::fromStdString(idle_name));
	idle_url_edit->setText(QString::fromStdString(idle_url));
	idle_int->setValue(idle_interval);
	react_name_edit->setText(QString::fromStdString(reactive_name));
	react_url_edit->setText(QString::fromStdString(reactive_url));
	react_cd2->setValue(reactive_cooldown);
	greet_dist_spin->setValue(greeting_distance);
	farewell_dist_spin->setValue(farewell_distance);
	talk_radius_spin->setValue(chat_radius);
	disabled_cb->setChecked((flags & BOT_DISABLED_FLAG) != 0);
	always_face_cb->setChecked((flags & BOT_ALWAYS_FACE_FLAG) != 0);
	stationary_cb->setChecked((flags & BOT_STATIONARY_FLAG) != 0);
	audio_url_edit->setText(QString::fromStdString(audio_url));
	audio_vol_spin->setValue(audio_volume);
	audio_radius_spin->setValue(audio_radius);
	audio_activation_spin->setValue(audio_activation_distance);
	audio_cooldown_spin->setValue(audio_cooldown);
	audio_loop_cb->setChecked((flags & BOT_AUDIO_LOOP_FLAG) != 0);
	audio_spatial_cb->setChecked((flags & BOT_AUDIO_SPATIAL_FLAG) != 0);
	audio_autoplay_cb->setChecked((flags & BOT_AUDIO_AUTOPLAY_FLAG) != 0);
	trigger_proximity_cb->setChecked((trigger_flags & BOT_TRIGGER_PROXIMITY_FLAG) != 0);
	trigger_chat_cb->setChecked((trigger_flags & BOT_TRIGGER_CHAT_FLAG) != 0);
	trigger_keywords_cb->setChecked((trigger_flags & BOT_TRIGGER_KEYWORDS_FLAG) != 0);
	trigger_gesture_cb->setChecked((trigger_flags & BOT_TRIGGER_GESTURE_FLAG) != 0);
	trigger_use_cb->setChecked((trigger_flags & BOT_TRIGGER_USE_FLAG) != 0);
	trigger_keywords_edit->setText(QString::fromStdString(trigger_keywords));
	trigger_cooldown_spin->setValue(trigger_cooldown);

	// gesture flags (FLAG_LOOP=2, FLAG_ANIMATE_HEAD=1)
	greet_loop_cb->setChecked((greeting_gesture_flags & 2) != 0);
	greet_head_cb->setChecked((greeting_gesture_flags & 1) != 0);
	idle_loop_cb->setChecked((idle_gesture_flags & 2) != 0);
	idle_head_cb->setChecked((idle_gesture_flags & 1) != 0);
	react_loop_cb->setChecked((reactive_gesture_flags & 2) != 0);
	react_head_cb->setChecked((reactive_gesture_flags & 1) != 0);

	fallback_msg_edit->setText(QString::fromStdString(fallback_message));

	surprise_name_edit->setText(QString::fromStdString(surprise_name));
	surprise_url_edit->setText(QString::fromStdString(surprise_url));
	surprise_cd->setValue(surprise_cooldown);
	surprise_loop_cb->setChecked((surprise_flags & 2) != 0);
	surprise_head_cb->setChecked((surprise_flags & 1) != 0);

	acknowledge_name_edit->setText(QString::fromStdString(acknowledge_name));
	acknowledge_url_edit->setText(QString::fromStdString(acknowledge_url));
	acknowledge_cd->setValue(acknowledge_cooldown);
	acknowledge_loop_cb->setChecked((acknowledge_flags & 2) != 0);
	acknowledge_head_cb->setChecked((acknowledge_flags & 1) != 0);

	use_action_combo->setCurrentIndex((int)use_action_type);
	use_action_param_edit->setText(QString::fromStdString(use_action_param));
	api_key_edit->setText(QString::fromStdString(api_key));
	api_endpoint_edit->setText(QString::fromStdString(api_endpoint));

	if(bot_list_widget)
	{
		QSignalBlocker blocker(bot_list_widget);
		for(int i = 0; i < (int)bot_list_entries.size(); ++i)
			if(bot_list_entries[i].bot_id == bot_id)
			{
				bot_list_widget->setCurrentRow(i);
				break;
			}
	}

	show();
}


void BotEditorWidget::clear()
{
	bot_id = 0;
	avatar_uid = UID();
	name_edit->clear();
	avatar_url_edit->clear();
	scale_x_spin->setValue(1.0);
	scale_y_spin->setValue(1.0);
	scale_z_spin->setValue(1.0);
	prompt_edit->clear();
	ai_model_edit->clear();
	ai_preset_edit->setText("assistant");
	ai_knowledge_edit->clear();
	ai_temperature_spin->setValue(0.7);
	ai_max_tokens_spin->setValue(0);
	greet_name_edit->clear();
	greet_url_edit->clear();
	idle_name_edit->clear();
	idle_url_edit->clear();
	react_name_edit->clear();
	react_url_edit->clear();
	greet_cd->setValue(30);
	idle_int->setValue(20);
	react_cd2->setValue(15);
	greet_dist_spin->setValue(6);
	farewell_dist_spin->setValue(10);
	talk_radius_spin->setValue(8);
	disabled_cb->setChecked(false);
	always_face_cb->setChecked(false);
	stationary_cb->setChecked(true);
	audio_url_edit->clear();
	audio_vol_spin->setValue(1.0);
	audio_radius_spin->setValue(10.0);
	audio_activation_spin->setValue(12.0);
	audio_cooldown_spin->setValue(0.0);
	audio_autoplay_cb->setChecked(true);
	audio_loop_cb->setChecked(true);
	audio_spatial_cb->setChecked(true);
	trigger_proximity_cb->setChecked(true);
	trigger_chat_cb->setChecked(true);
	trigger_keywords_cb->setChecked(false);
	trigger_gesture_cb->setChecked(false);
	trigger_use_cb->setChecked(false);
	trigger_keywords_edit->clear();
	trigger_cooldown_spin->setValue(3.0);
	greet_loop_cb->setChecked(false);
	greet_head_cb->setChecked(false);
	idle_loop_cb->setChecked(false);
	idle_head_cb->setChecked(false);
	react_loop_cb->setChecked(false);
	react_head_cb->setChecked(false);
	fallback_msg_edit->clear();
	surprise_name_edit->clear();
	surprise_url_edit->clear();
	surprise_cd->setValue(15);
	surprise_loop_cb->setChecked(false);
	surprise_head_cb->setChecked(false);
	acknowledge_name_edit->clear();
	acknowledge_url_edit->clear();
	acknowledge_cd->setValue(10);
	acknowledge_loop_cb->setChecked(false);
	acknowledge_head_cb->setChecked(false);
	use_action_combo->setCurrentIndex(0);
	use_action_param_edit->clear();
	api_key_edit->clear();
	api_endpoint_edit->clear();
	{
		QSignalBlocker bx(pos_x_spin), by(pos_y_spin), bz(pos_z_spin), bh(heading_spin);
		pos_x_spin->setValue(0);
		pos_y_spin->setValue(0);
		pos_z_spin->setValue(1.70);
		heading_spin->setValue(0);
	}
	hide();
}


void BotEditorWidget::setBotList(const std::vector<BotListEntry>& bots)
{
	bot_list_entries = bots;
	if(!bot_list_widget)
		return;

	QSignalBlocker blocker(bot_list_widget);
	bot_list_widget->clear();
	for(const BotListEntry& entry : bot_list_entries)
	{
		QString label = QString::fromStdString(entry.name);
		if(label.isEmpty())
			label = QString("Bot #%1").arg((qulonglong)entry.bot_id);
		bot_list_widget->addItem(label);
		if(entry.bot_id == bot_id)
			bot_list_widget->setCurrentRow(bot_list_widget->count() - 1);
	}
}


void BotEditorWidget::updatePosition(double x, double y, double z)
{
	QSignalBlocker bx(pos_x_spin), by(pos_y_spin), bz(pos_z_spin);
	pos_x_spin->setValue(x);
	pos_y_spin->setValue(y);
	pos_z_spin->setValue(z);
}


void BotEditorWidget::onBotListCurrentRowChanged(int row)
{
	if(row < 0 || row >= (int)bot_list_entries.size())
		return;

	const BotListEntry& entry = bot_list_entries[(size_t)row];
	if(entry.bot_id != bot_id)
		emit botSelected(entry.bot_id, entry.avatar_uid);
}


void BotEditorWidget::onRefreshBots()
{
	if(gui_client)
		gui_client->queryUserBots();
}


void BotEditorWidget::scheduleMove()
{
	// Debounce: restart 150 ms timer on each value change
	if(bot_id != 0)
		move_timer->start();
}


void BotEditorWidget::sendMoveBot()
{
	if(!gui_client || bot_id == 0) return;
	const double heading_rad = heading_spin->value() * (3.14159265358979323846 / 180.0);
	gui_client->moveBot(bot_id,
		Vec3d(pos_x_spin->value(), pos_y_spin->value(), pos_z_spin->value()),
		(float)heading_rad);
}


void BotEditorWidget::onAvatarURLChanged()
{
	// Apply avatar URL immediately when editing finished (Enter / Tab)
	if(!gui_client || bot_id == 0) return;
	AvatarSettings av;
	{
		std::string avatar_url = avatar_url_edit->text().toStdString();
		if(!avatar_url.empty())
		{
			avatar_url = gui_client->uploadLocalFileForBot(avatar_url); // no-op if already sub://
			avatar_url_edit->setText(QString::fromStdString(avatar_url));
		}
		av.model_url = toURLString(avatar_url);
	}
	const Vec3f model_scale((float)scale_x_spin->value(), (float)scale_y_spin->value(), (float)scale_z_spin->value());
	av.pre_ob_to_world_matrix = Matrix4f::scaleMatrix(model_scale.x, model_scale.y, model_scale.z);
	std::string audio_url = audio_url_edit->text().toStdString();
	if(!audio_url.empty())
	{
		audio_url = gui_client->uploadLocalFileForBot(audio_url);
		audio_url_edit->setText(QString::fromStdString(audio_url));
	}
	uint32 flags = 0;
	if(disabled_cb->isChecked()) flags |= BOT_DISABLED_FLAG;
	if(always_face_cb->isChecked()) flags |= BOT_ALWAYS_FACE_FLAG;
	if(stationary_cb->isChecked()) flags |= BOT_STATIONARY_FLAG;
	if(audio_loop_cb->isChecked()) flags |= BOT_AUDIO_LOOP_FLAG;
	if(audio_spatial_cb->isChecked()) flags |= BOT_AUDIO_SPATIAL_FLAG;
	if(audio_autoplay_cb->isChecked()) flags |= BOT_AUDIO_AUTOPLAY_FLAG;
	uint32 trigger_flags = 0;
	if(trigger_proximity_cb->isChecked()) trigger_flags |= BOT_TRIGGER_PROXIMITY_FLAG;
	if(trigger_chat_cb->isChecked()) trigger_flags |= BOT_TRIGGER_CHAT_FLAG;
	if(trigger_keywords_cb->isChecked()) trigger_flags |= BOT_TRIGGER_KEYWORDS_FLAG;
	if(trigger_gesture_cb->isChecked()) trigger_flags |= BOT_TRIGGER_GESTURE_FLAG;
	if(trigger_use_cb->isChecked()) trigger_flags |= BOT_TRIGGER_USE_FLAG;
	callUpdateBot(av, audio_url, flags, trigger_flags, model_scale);
}


void BotEditorWidget::sendUpdateBot()
{
	if(!gui_client || bot_id == 0) return;
	AvatarSettings av;
	{
		std::string avatar_url = avatar_url_edit->text().toStdString();
		if(!avatar_url.empty())
		{
			avatar_url = gui_client->uploadLocalFileForBot(avatar_url); // no-op if already sub://
			avatar_url_edit->setText(QString::fromStdString(avatar_url));
		}
		av.model_url = toURLString(avatar_url);
	}
	const Vec3f model_scale((float)scale_x_spin->value(), (float)scale_y_spin->value(), (float)scale_z_spin->value());
	av.pre_ob_to_world_matrix = Matrix4f::scaleMatrix(model_scale.x, model_scale.y, model_scale.z);
	std::string audio_url = audio_url_edit->text().toStdString();
	if(!audio_url.empty())
	{
		audio_url = gui_client->uploadLocalFileForBot(audio_url);
		audio_url_edit->setText(QString::fromStdString(audio_url));
	}
	uint32 flags = 0;
	if(disabled_cb->isChecked()) flags |= BOT_DISABLED_FLAG;
	if(always_face_cb->isChecked()) flags |= BOT_ALWAYS_FACE_FLAG;
	if(stationary_cb->isChecked()) flags |= BOT_STATIONARY_FLAG;
	if(audio_loop_cb->isChecked()) flags |= BOT_AUDIO_LOOP_FLAG;
	if(audio_spatial_cb->isChecked()) flags |= BOT_AUDIO_SPATIAL_FLAG;
	if(audio_autoplay_cb->isChecked()) flags |= BOT_AUDIO_AUTOPLAY_FLAG;
	uint32 trigger_flags = 0;
	if(trigger_proximity_cb->isChecked()) trigger_flags |= BOT_TRIGGER_PROXIMITY_FLAG;
	if(trigger_chat_cb->isChecked()) trigger_flags |= BOT_TRIGGER_CHAT_FLAG;
	if(trigger_keywords_cb->isChecked()) trigger_flags |= BOT_TRIGGER_KEYWORDS_FLAG;
	if(trigger_gesture_cb->isChecked()) trigger_flags |= BOT_TRIGGER_GESTURE_FLAG;
	if(trigger_use_cb->isChecked()) trigger_flags |= BOT_TRIGGER_USE_FLAG;
	callUpdateBot(av, audio_url, flags, trigger_flags, model_scale);
}


void BotEditorWidget::callUpdateBot(const AvatarSettings& av, const std::string& audio_url,
	uint32 flags, uint32 trigger_flags, const Vec3f& model_scale)
{
	const uint32 greet_gflags = (greet_loop_cb->isChecked() ? 2u : 0u) | (greet_head_cb->isChecked() ? 1u : 0u);
	const uint32 idle_gflags  = (idle_loop_cb->isChecked()  ? 2u : 0u) | (idle_head_cb->isChecked()  ? 1u : 0u);
	const uint32 react_gflags = (react_loop_cb->isChecked() ? 2u : 0u) | (react_head_cb->isChecked() ? 1u : 0u);
	const uint32 surp_gflags  = (surprise_loop_cb->isChecked() ? 2u : 0u) | (surprise_head_cb->isChecked() ? 1u : 0u);
	const uint32 ack_gflags   = (acknowledge_loop_cb->isChecked() ? 2u : 0u) | (acknowledge_head_cb->isChecked() ? 1u : 0u);

	gui_client->updateBot(bot_id,
		name_edit->text().toStdString(),
		prompt_edit->toPlainText().toStdString(),
		av,
		greet_name_edit->text().toStdString(), greet_url_edit->text().toStdString(), (float)greet_cd->value(),
		idle_name_edit->text().toStdString(),  idle_url_edit->text().toStdString(),  (float)idle_int->value(),
		react_name_edit->text().toStdString(), react_url_edit->text().toStdString(), (float)react_cd2->value(),
		flags,
		(float)greet_dist_spin->value(), (float)farewell_dist_spin->value(), (float)talk_radius_spin->value(),
		model_scale,
		ai_model_edit->text().toStdString(), ai_preset_edit->text().toStdString(), ai_knowledge_edit->toPlainText().toStdString(), (float)ai_temperature_spin->value(), (uint32)ai_max_tokens_spin->value(),
		audio_url, (float)audio_vol_spin->value(), (float)audio_radius_spin->value(), (float)audio_activation_spin->value(), (float)audio_cooldown_spin->value(),
		trigger_flags, trigger_keywords_edit->text().toStdString(), (float)trigger_cooldown_spin->value(),
		greet_gflags, idle_gflags, react_gflags,
		fallback_msg_edit->text().toStdString(),
		surprise_name_edit->text().toStdString(), surprise_url_edit->text().toStdString(), surp_gflags, (float)surprise_cd->value(),
		acknowledge_name_edit->text().toStdString(), acknowledge_url_edit->text().toStdString(), ack_gflags, (float)acknowledge_cd->value(),
		(uint32)use_action_combo->currentIndex(),
		use_action_param_edit->text().toStdString(),
		api_key_edit->text().toStdString(),
		api_endpoint_edit->text().toStdString()
	);
}


void BotEditorWidget::onSave()
{
	sendMoveBot();    // save current position
	sendUpdateBot();  // save name, avatar, prompt, animations
	emit saveClicked();
}


void BotEditorWidget::onDelete()
{
	if(!gui_client || bot_id == 0) return;
	gui_client->deleteBotImmediate(bot_id, avatar_uid);
	clear();
	emit deleteClicked();
}


void BotEditorWidget::onCancel()
{
	clear();
	emit cancelClicked();
}


void BotEditorWidget::onBrowseAvatar()
{
	const QString path = QFileDialog::getOpenFileName(this,
		"Выбрать модель аватара", {},
		"3D модели (*.glb *.gltf *.obj *.vox *.bmesh)");
	if(path.isEmpty())
		return;

	std::string url = path.toStdString();
	if(gui_client)
		url = gui_client->uploadLocalFileForBot(url); // copy to resources dir, get sub:// URL

	avatar_url_edit->setText(QString::fromStdString(url));
	onAvatarURLChanged();
}
