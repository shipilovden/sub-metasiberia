/*=====================================================================
PhotoVideoSettingsPanel.cpp
---------------------------
Native Qt photo and video settings panel for Metasiberia.
=====================================================================*/


#include "PhotoVideoSettingsPanel.h"
#include "LucideIconUtils.h"


#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QEvent>
#include <QtGui/QPalette>
#include <QtGui/QImageWriter>
#include <QtGui/QPixmap>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <cmath>


namespace
{
const QString settingsRoot()
{
	return QStringLiteral("photo_video_editor");
}


QString presetGroupKey(const QString& preset_name)
{
	return QString::fromLatin1(preset_name.trimmed().toUtf8().toHex());
}


QToolButton* makeToolButton(QWidget* parent, const QString& tooltip)
{
	QToolButton* button = new QToolButton(parent);
	button->setAutoRaise(true);
	button->setToolTip(tooltip);
	button->setStatusTip(tooltip);
	return button;
}
}


PhotoVideoSettingsPanel::PhotoVideoSettingsPanel(QSettings* settings_, QWidget* parent)
:
	QWidget(parent),
	settings(settings_),
	restoring_state(true),
	preset_combo(nullptr),
	save_preset_button(nullptr),
	delete_preset_button(nullptr),
	tabs(nullptr),
	camera_mode_combo(nullptr),
	autofocus_mode_combo(nullptr),
	dof_blur_spin(nullptr),
	focus_distance_spin(nullptr),
	ev_spin(nullptr),
	saturation_spin(nullptr),
	focal_length_spin(nullptr),
	roll_spin(nullptr),
	grid_check(nullptr),
	hide_ui_check(nullptr),
	resolution_combo(nullptr),
	frame_rate_spin(nullptr),
	codec_combo(nullptr),
	bitrate_spin(nullptr),
	quality_combo(nullptr),
	stabilisation_check(nullptr),
	microphone_check(nullptr),
	system_audio_check(nullptr),
	maximum_duration_spin(nullptr),
	image_format_combo(nullptr),
	colour_space_combo(nullptr),
	output_directory_edit(nullptr),
	timestamp_check(nullptr),
	metadata_check(nullptr),
	reset_button(nullptr),
	gallery_button(nullptr),
	capture_button(nullptr),
	record_button(nullptr)
{
	setObjectName(QStringLiteral("photoVideoSettingsPanel"));
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	setMinimumWidth(330);

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(7);

	QHBoxLayout* preset_row = new QHBoxLayout();
	preset_row->addWidget(new QLabel(tr("Пресет:"), this));
	preset_combo = new QComboBox(this);
	preset_combo->setObjectName(QStringLiteral("photoPreset"));
	preset_combo->setEditable(true);
	preset_combo->setInsertPolicy(QComboBox::NoInsert);
	preset_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	preset_combo->setMinimumContentsLength(8);
	preset_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	save_preset_button = makeToolButton(this, tr("Сохранить пресет фото и видео"));
	save_preset_button->setText(QStringLiteral("✓"));
	save_preset_button->setObjectName(QStringLiteral("photoSavePreset"));
	delete_preset_button = makeToolButton(this, tr("Удалить выбранный пресет"));
	delete_preset_button->setText(QStringLiteral("×"));
	preset_row->addWidget(preset_combo, 1);
	preset_row->addWidget(save_preset_button);
	preset_row->addWidget(delete_preset_button);
	root->addLayout(preset_row);

	QComboBox* sections = new QComboBox(this);
	sections->setObjectName(QStringLiteral("photoSection"));
	sections->addItems({tr("Оптика"), tr("Цвет"), tr("Эффекты"), tr("Композиция"), tr("Видео"), tr("Вывод")});
	root->addWidget(sections);
	tabs = new QStackedWidget(this);
	auto add_section = [this](QWidget* content) {
		QScrollArea* scroll = new QScrollArea(tabs);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		scroll->setWidget(content);
		tabs->addWidget(scroll);
	};
	add_section(makeCameraTab());
	add_section(colour_page);
	add_section(effects_page);
	add_section(composition_page);
	video_page = makeVideoTab();
	add_section(video_page);
	add_section(makeOutputTab());
	root->addWidget(tabs, 1);
	compare_button = new QPushButton(tr("До / после: цвет и эффекты"), this);
	compare_button->setObjectName(QStringLiteral("photoCompareOriginal"));
	compare_button->setToolTip(tr("Удерживайте для сравнения только цвета и эффектов. Оптика, фокус и композиция сохраняются."));
	compare_button->hide();
	compare_button->installEventFilter(this);
	installEventFilter(this);
	root->addWidget(compare_button);
	connect(compare_button, &QPushButton::pressed, this, [this]() { setCompareOriginal(true); });
	connect(compare_button, &QPushButton::released, this, [this]() { setCompareOriginal(false); });
	connect(sections, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		setCompareOriginal(false);
		tabs->setCurrentIndex(index);
		compare_button->setVisible(index == 1 || index == 2);
	});

	QHBoxLayout* bottom_row = new QHBoxLayout();
	reset_button = new QPushButton(tr("Сбросить"), this);
	reset_button->setObjectName(QStringLiteral("photoReset"));
	gallery_button = new QPushButton(tr("Галерея"), this);
	capture_button = new QPushButton(tr("Снимок"), this);
	capture_button->setObjectName(QStringLiteral("photoCapture"));
	record_button = new QPushButton(this);
	record_button->setCheckable(true);
	record_button->setToolTip(tr("Начать или остановить видеозапись"));
	record_button->setObjectName(QStringLiteral("photoRecord"));
	record_button->setToolTip(tr("Записать сцену в MP4; звук включается отдельно в разделе «Видео»"));
#ifndef _WIN32
	record_button->setEnabled(false);
	record_button->setToolTip(tr("Запись MP4 доступна только в Windows"));
#endif
	bottom_row->addWidget(reset_button);
	bottom_row->addWidget(gallery_button);
	root->addLayout(bottom_row);
	QHBoxLayout* capture_row = new QHBoxLayout();
	capture_row->addWidget(capture_button);
	capture_row->addWidget(record_button);
	root->addLayout(capture_row);

	settings_save_timer.setSingleShot(true);
	settings_save_timer.setInterval(250);
	connect(&settings_save_timer, &QTimer::timeout, this, [this]() {
		if(settings)
			settings->setValue(settingsRoot() + QStringLiteral("/current_state"), captureState());
	});
	connect(preset_combo, QOverload<int>::of(&QComboBox::activated), this, [this](int) { loadPreset(preset_combo->currentText()); });
	connect(save_preset_button, &QToolButton::clicked, this, &PhotoVideoSettingsPanel::saveCurrentPreset);
	connect(delete_preset_button, &QToolButton::clicked, this, &PhotoVideoSettingsPanel::deleteCurrentPreset);
	connect(reset_button, &QPushButton::clicked, this, [this]() {
		cancelCapture();
		resetControls();
		emit resetRequested(captureState());
	});
	connect(gallery_button, &QPushButton::clicked, this, &PhotoVideoSettingsPanel::browseGalleryRequested);
	capture_timer.setInterval(1000);
	connect(&capture_timer, &QTimer::timeout, this, [this]() {
		if(--capture_seconds_remaining <= 0)
		{
			cancelCapture();
			emit capturePhotoRequested(captureState());
		}
		else
			capture_button->setText(tr("Отмена (%1 с)").arg(capture_seconds_remaining));
	});
	connect(capture_button, &QPushButton::clicked, this, [this]() {
		if(capture_timer.isActive()) { cancelCapture(); return; }
		capture_seconds_remaining = capture_delay_combo->currentData().toInt();
		if(capture_seconds_remaining == 0)
			emit capturePhotoRequested(captureState());
		else
		{
			capture_button->setText(tr("Отмена (%1 с)").arg(capture_seconds_remaining));
			capture_timer.start();
		}
	});
	connect(record_button, &QPushButton::toggled, this, [this](bool recording) {
		updateRecordButton();
		emit recordingChanged(recording, captureState());
	});

	setStyleSheet(QStringLiteral(
		"QToolButton:hover, QPushButton:hover { background: palette(highlight); color: palette(highlighted-text); }"
		"QPushButton:checked { background: palette(highlight); color: palette(highlighted-text); border: 1px solid palette(highlight); }"));

	refreshPresets();
	const QString last_preset = settings ? settings->value(settingsRoot() + QStringLiteral("/last_preset"), tr("По умолчанию")).toString() : tr("По умолчанию");
	const int last_index = preset_combo->findText(last_preset);
	if(last_index >= 0)
		preset_combo->setCurrentIndex(last_index);
	else
		preset_combo->setEditText(last_preset);

	QVariantMap initial_state;
	if(settings)
	{
		initial_state = settings->value(settingsRoot() + QStringLiteral("/current_state")).toMap();
		if(initial_state.isEmpty())
		{
			settings->beginGroup(settingsRoot() + QStringLiteral("/presets/") + presetGroupKey(last_preset));
			initial_state = settings->value(QStringLiteral("state")).toMap();
			settings->endGroup();
		}
	}
	if(initial_state.isEmpty())
		resetControls();
	else
		restoreState(initial_state);
	updateRecordButton();
	applyIcons();
}


QWidget* PhotoVideoSettingsPanel::makeCameraTab()
{
	QWidget* content = new QWidget(this);
	QVBoxLayout* content_layout = new QVBoxLayout(content);
	content_layout->setContentsMargins(5, 5, 5, 5);
	colour_page = new QWidget(this);
	effects_page = new QWidget(this);
	composition_page = new QWidget(this);
	QVBoxLayout* colour_layout = new QVBoxLayout(colour_page);
	QVBoxLayout* effects_layout = new QVBoxLayout(effects_page);
	QVBoxLayout* composition_layout = new QVBoxLayout(composition_page);
	for(QVBoxLayout* layout : {colour_layout, effects_layout, composition_layout})
		layout->setContentsMargins(5, 5, 5, 5);

	QGroupBox* camera_group = new QGroupBox(tr("Режим камеры"), content);
	QFormLayout* camera_layout = new QFormLayout(camera_group);
	camera_layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	camera_mode_combo = new QComboBox(camera_group);
	camera_mode_combo->setObjectName(QStringLiteral("photoCameraMode"));
	camera_mode_combo->addItem(tr("Стандартная камера"), QStringLiteral("standard"));
	camera_mode_combo->addItem(tr("Селфи-камера"), QStringLiteral("selfie"));
	camera_mode_combo->addItem(tr("Фиксированный угол"), QStringLiteral("fixed_angle"));
	camera_mode_combo->addItem(tr("Свободная камера"), QStringLiteral("free"));
	camera_mode_combo->addItem(tr("Камера слежения"), QStringLiteral("tracking"));
	autofocus_mode_combo = new QComboBox(camera_group);
	autofocus_mode_combo->setObjectName(QStringLiteral("photoAutofocus"));
	autofocus_mode_combo->addItem(tr("Выключен"), QStringLiteral("off"));
	autofocus_mode_combo->addItem(tr("По глазам"), QStringLiteral("eye"));
	camera_layout->addRow(tr("Камера"), camera_mode_combo);
	camera_layout->addRow(tr("Автофокус"), autofocus_mode_combo);
	content_layout->addWidget(camera_group);

	QGroupBox* optics_group = new QGroupBox(tr("Объектив и фокус"), content);
	QVBoxLayout* optics_layout = new QVBoxLayout(optics_group);
	QComboBox* looks = new QComboBox(optics_group);
	looks->setObjectName(QStringLiteral("photoLook"));
	looks->addItems({tr("Применить образ…"), tr("Нейтральный"), tr("Чёрно-белый"), tr("Тёплая плёнка"), tr("Холодный вечер"), tr("Яркие цвета"), tr("Мягкое свечение"), tr("Винтаж")});
	looks->addItems({tr("Кино: бирюза / оранжевый"), tr("Матовая плёнка"), tr("Нуар")});
	colour_layout->addWidget(looks);
	colour_layout->addWidget(makeProSlider("filter_strength", tr("Сила обработки цвета и эффектов"), 0, 1, 1));
	optics_layout->addWidget(makeSliderRow(tr("Размытие глубины резкости"), dof_blur_spin, 0.0, 1.0, 0.01, 2, QString()));
	optics_layout->addWidget(makeSliderRow(tr("Дистанция фокуса"), focus_distance_spin, 0.1, 100.0, 0.1, 1, tr(" м"), 3.0));
	colour_layout->addWidget(makeSliderRow(tr("Экспозиция (EV)"), ev_spin, -5.0, 5.0, 0.1, 1, QString()));
	colour_layout->addWidget(makeSliderRow(tr("Насыщенность"), saturation_spin, 0.0, 2.0, 0.05, 2, QString(), 1.0));
	optics_layout->addWidget(makeSliderRow(tr("Фокусное расстояние"), focal_length_spin, 12.0, 200.0, 1.0, 0, tr(" мм"), 25.0));
	composition_layout->addWidget(makeSliderRow(tr("Наклон камеры"), roll_spin, -180.0, 180.0, 1.0, 1, QStringLiteral("°")));
	effects_layout->addWidget(makeSliderRow(tr("Свечение ярких участков (Bloom)"), bloom_spin, 0.0, 1.0, 0.01, 2, QString()));
	colour_layout->addWidget(makeSliderRow(tr("Холодный / тёплый тон"), warmth_spin, -1.0, 1.0, 0.01, 2, QString()));
	colour_layout->addWidget(makeSliderRow(tr("Зелёный / пурпурный тон"), tint_spin, -1.0, 1.0, 0.01, 2, QString()));
	effects_layout->addWidget(makeSliderRow(tr("Виньетка"), vignette_spin, 0.0, 1.0, 0.01, 2, QString()));
	composition_layout->addWidget(makeSliderRow(tr("Сдвиг объектива по горизонтали"), shift_x_spin, -0.5, 0.5, 0.01, 2, QString()));
	composition_layout->addWidget(makeSliderRow(tr("Сдвиг объектива по вертикали"), shift_y_spin, -0.5, 0.5, 0.01, 2, QString()));
	warmth_spin->setToolTip(tr("Художественное тонирование, не физический баланс белого"));
	connect(looks, QOverload<int>::of(&QComboBox::activated), this, [this, looks](int index) {
		if(index == 0) return;
		restoring_state = true;
		ev_spin->setValue(0); saturation_spin->setValue(1); bloom_spin->setValue(0);
		warmth_spin->setValue(0); tint_spin->setValue(0); vignette_spin->setValue(0);
		// Recipes replace grading/effects only; camera, guides, trajectory and audio stay intact.
		for(auto it = pro_sliders.cbegin(); it != pro_sliders.cend(); ++it)
			if(it.key() != QStringLiteral("trajectory_duration"))
				it.value()->setValue(pro_defaults.value(it.key()).toDouble());
		pro_checks.value("grain_colour")->setChecked(false);
		lut_path_edit->clear();
		if(index == 2) { saturation_spin->setValue(0); vignette_spin->setValue(0.3); }
		if(index == 3) { saturation_spin->setValue(0.85); warmth_spin->setValue(0.6); bloom_spin->setValue(0.12); }
		if(index == 4) { warmth_spin->setValue(-0.5); saturation_spin->setValue(0.8); ev_spin->setValue(-0.3); }
		if(index == 5) { saturation_spin->setValue(1.35); ev_spin->setValue(0.1); }
		if(index == 6) { bloom_spin->setValue(0.45); ev_spin->setValue(0.2); saturation_spin->setValue(0.85); }
		if(index == 7) { warmth_spin->setValue(0.7); saturation_spin->setValue(0.5); vignette_spin->setValue(0.65); }
		if(index == 8) {
			pro_sliders.value("shadow_hue")->setValue(190); pro_sliders.value("highlight_hue")->setValue(35);
			pro_sliders.value("split_strength")->setValue(0.35); pro_sliders.value("contrast")->setValue(0.18);
			saturation_spin->setValue(0.9);
		}
		if(index == 9) {
			pro_sliders.value("blacks")->setValue(0.2); pro_sliders.value("highlights")->setValue(-0.2);
			pro_sliders.value("contrast")->setValue(-0.12); pro_sliders.value("grain")->setValue(0.18);
			saturation_spin->setValue(0.75); warmth_spin->setValue(0.15);
		}
		if(index == 10) {
			saturation_spin->setValue(0); vignette_spin->setValue(0.4);
			pro_sliders.value("contrast")->setValue(0.45); pro_sliders.value("blacks")->setValue(-0.2);
			pro_sliders.value("grain")->setValue(0.12);
		}
		restoring_state = false;
		looks->setCurrentIndex(0);
		controlsChanged();
	});
	QComboBox* lens_presets = new QComboBox(optics_group);
	lens_presets->addItem(tr("Быстрый выбор объектива…"), 0);
	for(int mm : {18, 25, 35, 50, 85, 135})
		lens_presets->addItem(tr("%1 мм").arg(mm), mm);
	optics_layout->addWidget(lens_presets);
	connect(lens_presets, QOverload<int>::of(&QComboBox::activated), this, [this, lens_presets](int) {
		const int mm = lens_presets->currentData().toInt();
		if(mm > 0) focal_length_spin->setValue(mm);
		lens_presets->setCurrentIndex(0);
	});
	content_layout->addWidget(optics_group);

	QGroupBox* overlay_group = new QGroupBox(tr("Интерфейс"), content);
	QVBoxLayout* overlay_layout = new QVBoxLayout(overlay_group);
	grid_check = new QCheckBox(tr("Композиционная сетка"), overlay_group);
	hide_ui_check = new QCheckBox(tr("Скрывать интерфейс мира"), overlay_group);
	hide_ui_check->setChecked(true);
	overlay_layout->addWidget(grid_check);
	overlay_layout->addWidget(hide_ui_check);
	aspect_ratio_combo = new QComboBox(overlay_group);
	aspect_ratio_combo->setObjectName(QStringLiteral("photoAspectRatio"));
	aspect_ratio_combo->addItem(tr("Кадр: весь вид"), 0.0);
	aspect_ratio_combo->addItem(tr("Кадр: 16:9 — широкий"), 16.0 / 9.0);
	aspect_ratio_combo->addItem(tr("Кадр: 4:3 — классический"), 4.0 / 3.0);
	aspect_ratio_combo->addItem(tr("Кадр: 1:1 — квадрат"), 1.0);
	aspect_ratio_combo->addItem(tr("Кадр: 9:16 — вертикальный"), 9.0 / 16.0);
	aspect_ratio_combo->setToolTip(tr("Обрезка по рамке предпросмотра с сохранением пропорций. Размер снимка задаётся в разделе «Вывод»."));
	overlay_layout->addWidget(aspect_ratio_combo);
	capture_delay_combo = new QComboBox(overlay_group);
	capture_delay_combo->setObjectName(QStringLiteral("photoCaptureDelay"));
	capture_delay_combo->addItem(tr("Таймер: без задержки"), 0);
	for(int seconds : {3, 5, 10})
		capture_delay_combo->addItem(tr("Таймер: %1 с").arg(seconds), seconds);
	overlay_layout->addWidget(capture_delay_combo);
	QPushButton* upload_button = new QPushButton(tr("Загрузить последнее фото…"), overlay_group);
	upload_button->setToolTip(tr("Открыть подтверждение загрузки последнего сохранённого фото на сервер"));
	overlay_layout->addWidget(upload_button);
	connect(upload_button, &QPushButton::clicked, this, &PhotoVideoSettingsPanel::uploadPhotoRequested);
	connect(aspect_ratio_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { controlsChanged(); });
	connect(capture_delay_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { controlsChanged(); });
	composition_layout->addWidget(overlay_group);
	content_layout->addStretch(1);

	colour_layout->addWidget(makeProSlider("contrast", tr("Контраст"), -1, 1, 0));
	colour_layout->addWidget(makeProSlider("shadows", tr("Тени"), -1, 1, 0));
	colour_layout->addWidget(makeProSlider("highlights", tr("Светлые участки"), -1, 1, 0));
	colour_layout->addWidget(makeProSlider("blacks", tr("Чёрные"), -1, 1, 0));
	colour_layout->addWidget(makeProSlider("whites", tr("Белые"), -1, 1, 0));
	QWidget* white_balance = makeProSlider("temperature_kelvin", tr("Баланс белого, K"), 2000, 12000, 6500, 100, 0);
	white_balance->setToolTip(tr("Температура освещения для коррекции к нейтральному белому. Низкие значения убирают тёплый оттенок, высокие — холодный. 6500 K — без коррекции."));
	colour_layout->addWidget(white_balance);
	colour_layout->addWidget(makeProSlider("wb_tint", tr("Оттенок баланса белого"), -1, 1, 0));
	colour_layout->addWidget(makeProSlider("shadow_hue", tr("Тонирование теней, °"), 0, 360, 0, 1, 0));
	colour_layout->addWidget(makeProSlider("highlight_hue", tr("Тонирование света, °"), 0, 360, 35, 1, 0));
	colour_layout->addWidget(makeProSlider("split_strength", tr("Сила раздельного тонирования"), 0, 1, 0));
	QHBoxLayout* lut_row = new QHBoxLayout();
	lut_path_edit = new QLineEdit(colour_page);
	lut_path_edit->setObjectName(QStringLiteral("photoLutPath"));
	lut_path_edit->setReadOnly(true);
	lut_path_edit->setPlaceholderText(tr("LUT не выбран"));
	lut_path_edit->setToolTip(tr("3D .cube размером 2–64, входной диапазон 0–1. 1D и HDR/shaper LUT не поддерживаются."));
	QPushButton* lut_browse = new QPushButton(tr(".cube…"), colour_page);
	lut_browse->setObjectName(QStringLiteral("photoLutBrowse"));
	QToolButton* lut_clear = makeToolButton(colour_page, tr("Убрать LUT"));
	lut_clear->setObjectName(QStringLiteral("photoLutClear"));
	lut_clear->setText(QStringLiteral("×"));
	lut_row->addWidget(lut_path_edit, 1);
	lut_row->addWidget(lut_browse);
	lut_row->addWidget(lut_clear);
	colour_layout->addLayout(lut_row);
	connect(lut_browse, &QPushButton::clicked, this, [this]() {
		const QString path = QFileDialog::getOpenFileName(this, tr("Выбрать LUT"), lut_path_edit->text(), tr("Таблица цвета (*.cube)"));
		if(!path.isEmpty()) lut_path_edit->setText(path);
	});
	connect(lut_clear, &QToolButton::clicked, lut_path_edit, &QLineEdit::clear);
	connect(lut_path_edit, &QLineEdit::textChanged, this, [this]() { controlsChanged(); });
	colour_layout->addWidget(makeProSlider("lut_strength", tr("Сила LUT"), 0, 1, 1));
	colour_layout->addStretch(1);

	effects_layout->addWidget(makeProSlider("grain", tr("Зерно"), 0, 1, 0));
	effects_layout->addWidget(makeProSlider("grain_size", tr("Размер зерна"), 0.5, 4, 1));
	effects_layout->addWidget(makeProCheck("grain_colour", tr("Цветное зерно")));
	effects_layout->addWidget(makeProSlider("vignette_radius", tr("Радиус виньетки"), 0.2, 1.5, 0.75));
	effects_layout->addWidget(makeProSlider("vignette_softness", tr("Мягкость виньетки"), 0.05, 1, 0.5));
	effects_layout->addWidget(makeProSlider("vignette_roundness", tr("Округлость виньетки"), 0, 1, 1));
	effects_layout->addWidget(makeProSlider("vignette_center_x", tr("Центр виньетки: X"), -0.5, 0.5, 0));
	effects_layout->addWidget(makeProSlider("vignette_center_y", tr("Центр виньетки: Y"), -0.5, 0.5, 0));
	effects_layout->addWidget(makeProSlider("diffusion", tr("Диффузия"), 0, 1, 0));
	effects_layout->addWidget(makeProSlider("halation", tr("Ореолы плёнки"), 0, 1, 0));
	effects_layout->addWidget(makeProSlider("aberration", tr("Хроматическая аберрация"), 0, 1, 0));
	effects_layout->addWidget(makeProSlider("distortion", tr("Дисторсия"), -0.5, 0.5, 0));
	effects_layout->addStretch(1);

	QPushButton* focus_pick = new QPushButton(tr("Выбрать точку фокуса в сцене"), composition_page);
	focus_pick->setObjectName(QStringLiteral("photoFocusPick"));
	focus_pick->setToolTip(tr("Нажмите, затем укажите точку в сцене для ручной фокусировки"));
	composition_layout->addWidget(focus_pick);
	connect(focus_pick, &QPushButton::clicked, this, &PhotoVideoSettingsPanel::focusPickRequested);
	composition_layout->addWidget(makeProCheck("clipping_zebra", tr("Зебра пересветов и провалов")));
	QWidget* peaking = makeProCheck("focus_peaking", tr("Подсветка резких границ"));
	peaking->setToolTip(tr("Оценка контраста границ изображения, а не измерение глубины. Помогает выбрать фокус; в файл не записывается."));
	composition_layout->addWidget(peaking);
	composition_layout->addWidget(makeProCheck("show_histogram", tr("Гистограмма")));
	histogram_label = new QLabel(tr("Ожидание гистограммы"), composition_page);
	histogram_label->setObjectName(QStringLiteral("photoHistogram"));
	histogram_label->setAlignment(Qt::AlignCenter);
	histogram_label->setFixedHeight(100);
	histogram_label->setMinimumWidth(1);
	histogram_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	composition_layout->addWidget(histogram_label);
	composition_layout->addStretch(1);

	connect(camera_mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		if(!restoring_state)
			emit cameraModeChanged(camera_mode_combo->currentData().toString());
		controlsChanged();
	});
	connect(autofocus_mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		if(!restoring_state)
			emit autofocusModeChanged(autofocus_mode_combo->currentData().toString());
		controlsChanged();
	});
	connect(grid_check, &QCheckBox::toggled, this, [this](bool) { controlsChanged(); });
	connect(hide_ui_check, &QCheckBox::toggled, this, [this](bool) { controlsChanged(); });
	return content;
}


QWidget* PhotoVideoSettingsPanel::makeSliderRow(const QString& label, QDoubleSpinBox*& spin,
	double minimum, double maximum, double step, int decimals, const QString& suffix, double default_value)
{
	QWidget* row = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	QLabel* value_label = new QLabel(label, row);
	value_label->setWordWrap(true);
	QSlider* slider = new QSlider(Qt::Horizontal, row);
	int multiplier = 1;
	for(int i=0; i<decimals; ++i)
		multiplier *= 10;
	slider->setRange(qRound(minimum * multiplier), qRound(maximum * multiplier));
	slider->setSingleStep(qMax(1, qRound(step * multiplier)));
	spin = new QDoubleSpinBox(row);
	spin->setRange(minimum, maximum);
	spin->setSingleStep(step);
	spin->setDecimals(decimals);
	spin->setSuffix(suffix);
	spin->setMinimumWidth(85);
	spin->setValue(default_value);
	spin->setProperty("defaultValue", default_value);
	value_label->setBuddy(spin);
	slider->setAccessibleName(label);
	slider->setValue(qRound(spin->value() * multiplier));
	layout->addWidget(value_label);
	QHBoxLayout* values_layout = new QHBoxLayout();
	values_layout->addWidget(slider, 1);
	values_layout->addWidget(spin);
	QToolButton* reset = makeToolButton(row, tr("Сбросить «%1»: %2%3").arg(label).arg(default_value).arg(suffix));
	reset->setObjectName(QStringLiteral("photoSliderReset"));
	reset->setText(QStringLiteral("↺"));
	reset->setAccessibleName(reset->toolTip());
	values_layout->addWidget(reset);
	connect(reset, &QToolButton::clicked, this, [spin, default_value]() { spin->setValue(default_value); });
	layout->addLayout(values_layout);

	connect(slider, &QSlider::valueChanged, this, [this, spin, multiplier](int value) {
		const QSignalBlocker blocker(spin);
		spin->setValue(value / static_cast<double>(multiplier));
		controlsChanged();
	});
	connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, slider, multiplier](double value) {
		const QSignalBlocker blocker(slider);
		slider->setValue(qRound(value * multiplier));
		controlsChanged();
	});
	return row;
}


QWidget* PhotoVideoSettingsPanel::makeProSlider(const QString& key, const QString& label,
	double minimum, double maximum, double default_value, double step, int decimals)
{
	QDoubleSpinBox* spin = nullptr;
	QWidget* row = makeSliderRow(label, spin, minimum, maximum, step, decimals, QString(), default_value);
	spin->setObjectName(QStringLiteral("photo_") + key);
	pro_sliders.insert(key, spin);
	pro_defaults.insert(key, default_value);
	return row;
}


QCheckBox* PhotoVideoSettingsPanel::makeProCheck(const QString& key, const QString& label)
{
	QCheckBox* check = new QCheckBox(label, this);
	check->setObjectName(QStringLiteral("photo_") + key);
	pro_checks.insert(key, check);
	connect(check, &QCheckBox::toggled, this, [this]() { controlsChanged(); });
	return check;
}


QWidget* PhotoVideoSettingsPanel::makeVideoTab()
{
	QWidget* content = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(content);
	layout->setContentsMargins(5, 5, 5, 5);

	QGroupBox* format_group = new QGroupBox(tr("Параметры записи"), content);
	format_group->setObjectName(QStringLiteral("photoVideoFormat"));
	QFormLayout* format_layout = new QFormLayout(format_group);
	format_layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	resolution_combo = new QComboBox(format_group);
	resolution_combo->addItem(QStringLiteral("1280 × 720"), QStringLiteral("1280x720"));
	resolution_combo->addItem(QStringLiteral("1920 × 1080"), QStringLiteral("1920x1080"));
	resolution_combo->addItem(QStringLiteral("2560 × 1440"), QStringLiteral("2560x1440"));
	resolution_combo->addItem(QStringLiteral("3840 × 2160"), QStringLiteral("3840x2160"));
	resolution_combo->setCurrentIndex(1);
	frame_rate_spin = new QSpinBox(format_group);
	frame_rate_spin->setRange(15, 60);
	frame_rate_spin->setValue(30);
	frame_rate_spin->setSuffix(QStringLiteral(" fps"));
	codec_combo = new QComboBox(format_group);
	codec_combo->addItem(QStringLiteral("H.264"), QStringLiteral("h264"));
	codec_combo->setToolTip(tr("MP4 / H.264 через Windows Media Foundation"));
	bitrate_spin = new QSpinBox(format_group);
	bitrate_spin->setRange(1, 200);
	bitrate_spin->setValue(20);
	bitrate_spin->setSuffix(tr(" Мбит/с"));
	quality_combo = new QComboBox(format_group);
	quality_combo->addItem(tr("Черновое"), QStringLiteral("draft"));
	quality_combo->addItem(tr("Стандартное"), QStringLiteral("standard"));
	quality_combo->addItem(tr("Высокое"), QStringLiteral("high"));
	quality_combo->addItem(tr("Максимальное"), QStringLiteral("maximum"));
	quality_combo->setCurrentIndex(2);
	quality_combo->setEnabled(false);
	quality_combo->setToolTip(tr("Этот выбор не поддерживается. Качество записи задаётся битрейтом."));
	maximum_duration_spin = new QSpinBox(format_group);
	maximum_duration_spin->setRange(0, 1440);
	maximum_duration_spin->setSpecialValueText(tr("Без ограничения"));
	maximum_duration_spin->setSuffix(tr(" мин"));
	format_layout->addRow(tr("Разрешение"), resolution_combo);
	format_layout->addRow(tr("Частота кадров"), frame_rate_spin);
	format_layout->addRow(tr("Кодек"), codec_combo);
	format_layout->addRow(tr("Битрейт"), bitrate_spin);
	format_layout->addRow(tr("Качество (не поддерживается)"), quality_combo);
	format_layout->addRow(tr("Максимальная длительность"), maximum_duration_spin);
	layout->addWidget(format_group);

	QGroupBox* options_group = new QGroupBox(tr("Видео и звук"), content);
	options_group->setObjectName(QStringLiteral("photoVideoAudio"));
	QVBoxLayout* options_layout = new QVBoxLayout(options_group);
	stabilisation_check = new QCheckBox(tr("Стабилизация (не поддерживается)"), options_group);
	microphone_check = new QCheckBox(tr("Записывать микрофон"), options_group);
	microphone_check->setObjectName(QStringLiteral("photoMicrophone"));
	system_audio_check = new QCheckBox(tr("Записывать звук мира"), options_group);
	system_audio_check->setObjectName(QStringLiteral("photoWorldAudio"));
	system_audio_check->setChecked(false);
	stabilisation_check->setEnabled(false);
	stabilisation_check->setToolTip(tr("Стабилизация камеры пока не поддерживается"));
	microphone_check->setToolTip(tr("Запись микрофона только после явного включения. Пресеты не включают звук автоматически."));
	system_audio_check->setToolTip(tr("Записывает итоговый звук аудиодвижка Метасибири, без звуков других программ. Прямой звук встроенного браузера CEF не входит в запись. Пресеты не включают звук автоматически."));
	QLabel* note = new QLabel(tr("MP4 / H.264. Звук включается отдельно перед записью. Разрешение — размер выходного видео; детализация зависит от размера окна сцены."), content);
#ifndef _WIN32
	format_group->setEnabled(false);
	options_group->setEnabled(false);
	note->setText(tr("Запись MP4 и звука доступна только в Windows."));
#endif
	note->setWordWrap(true);
	layout->addWidget(note);
	options_layout->addWidget(stabilisation_check);
	options_layout->addWidget(microphone_check);
	options_layout->addWidget(system_audio_check);
	layout->addWidget(options_group);
	QGroupBox* trajectory_group = new QGroupBox(tr("Траектория камеры"), content);
	QVBoxLayout* trajectory_layout = new QVBoxLayout(trajectory_group);
	QLabel* trajectory_note = new QLabel(tr("Добавляйте текущие положения свободной камеры с фокусом и объективом."), trajectory_group);
	trajectory_note->setWordWrap(true);
	trajectory_layout->addWidget(trajectory_note);
	trajectory_layout->addWidget(makeProSlider("trajectory_duration", tr("Длительность, с"), 1, 120, 8, 1, 1));
	trajectory_layout->addWidget(makeProCheck("trajectory_loop", tr("Повтор: вперёд / назад")));
	QHBoxLayout* keyframes_row = new QHBoxLayout();
	QHBoxLayout* playback_row = new QHBoxLayout();
	const QStringList actions = {"add", "remove", "clear", "play", "stop"};
	const QStringList labels = {tr("Добавить"), tr("Убрать последнюю"), tr("Очистить"), tr("Проиграть"), tr("Стоп")};
	for(int i = 0; i < actions.size(); ++i) {
		QPushButton* button = new QPushButton(labels[i], trajectory_group);
		button->setObjectName(QStringLiteral("photoTrajectory_") + actions[i]);
		const QString action = actions[i];
		connect(button, &QPushButton::clicked, this, [this, action]() { emit trajectoryActionRequested(action); });
		(i < 2 ? keyframes_row : playback_row)->addWidget(button);
	}
	trajectory_layout->addLayout(keyframes_row);
	trajectory_layout->addLayout(playback_row);
	trajectory_status = new QLabel(tr("Добавьте ключевые кадры свободной камеры"), trajectory_group);
	trajectory_status->setObjectName(QStringLiteral("photoTrajectoryStatus"));
	trajectory_status->setWordWrap(true);
	trajectory_layout->addWidget(trajectory_status);
	layout->addWidget(trajectory_group);
	layout->addStretch(1);

	auto connect_combo = [this](QComboBox* combo) {
		connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { controlsChanged(); });
	};
	connect_combo(resolution_combo);
	connect_combo(codec_combo);
	connect_combo(quality_combo);
	connect(frame_rate_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { controlsChanged(); });
	connect(bitrate_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { controlsChanged(); });
	connect(maximum_duration_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { controlsChanged(); });
	connect(stabilisation_check, &QCheckBox::toggled, this, [this](bool) { controlsChanged(); });
	connect(microphone_check, &QCheckBox::toggled, this, [this](bool) { controlsChanged(); });
	connect(system_audio_check, &QCheckBox::toggled, this, [this](bool) { controlsChanged(); });
	return content;
}


QWidget* PhotoVideoSettingsPanel::makeOutputTab()
{
	QWidget* content = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(content);
	layout->setContentsMargins(5, 5, 5, 5);
	QGroupBox* output_group = new QGroupBox(tr("Файлы"), content);
	QFormLayout* output_layout = new QFormLayout(output_group);
	output_layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	photo_resolution_combo = new QComboBox(output_group);
	photo_resolution_combo->setObjectName(QStringLiteral("photoResolution"));
	photo_resolution_combo->addItem(tr("Размер окна сцены"), QStringLiteral("viewport"));
	for(const QString& resolution : {QStringLiteral("1920x1080"), QStringLiteral("3840x2160"), QStringLiteral("7680x4320")})
		photo_resolution_combo->addItem(resolution, resolution);
	photo_resolution_combo->setToolTip(tr("Рендер снимка в выбранном разрешении, затем обрезка по рамке композиции с сохранением пропорций. Это не увеличение готового снимка; крупный рендер требует больше памяти."));
	output_layout->addRow(tr("Разрешение снимка"), photo_resolution_combo);
	connect(photo_resolution_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { controlsChanged(); });
	image_format_combo = new QComboBox(output_group);
	image_format_combo->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
	image_format_combo->addItem(QStringLiteral("JPEG"), QStringLiteral("jpeg"));
	if(QImageWriter::supportedImageFormats().contains("webp"))
		image_format_combo->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
	colour_space_combo = new QComboBox(output_group);
	colour_space_combo->addItem(QStringLiteral("sRGB"), QStringLiteral("srgb"));
	colour_space_combo->setEnabled(false);
	colour_space_combo->setToolTip(tr("Снимок сохраняет sRGB-изображение сцены; HDR/P3-экспорт пока не поддерживается"));
	image_quality_spin = new QSpinBox(output_group);
	image_quality_spin->setRange(1, 100);
	image_quality_spin->setValue(95);
	image_quality_spin->setSuffix(QStringLiteral(" %"));
	image_quality_spin->setToolTip(tr("Качество JPEG/WebP. PNG всегда сохраняется без потерь."));
	QWidget* directory_widget = new QWidget(output_group);
	QHBoxLayout* directory_layout = new QHBoxLayout(directory_widget);
	directory_layout->setContentsMargins(0, 0, 0, 0);
	output_directory_edit = new QLineEdit(directory_widget);
	output_directory_edit->setPlaceholderText(tr("Каталог снимков и видео"));
	QToolButton* browse = makeToolButton(directory_widget, tr("Выбрать каталог"));
	browse->setText(QStringLiteral("…"));
	directory_layout->addWidget(output_directory_edit, 1);
	directory_layout->addWidget(browse);
	output_layout->addRow(tr("Формат снимка"), image_format_combo);
	output_layout->addRow(tr("Качество JPEG/WebP"), image_quality_spin);
	output_layout->addRow(tr("Цветовое пространство"), colour_space_combo);
	output_layout->addRow(tr("Каталог"), directory_widget);
	layout->addWidget(output_group);

	QGroupBox* metadata_group = new QGroupBox(tr("Дополнительные данные"), content);
	QVBoxLayout* metadata_layout = new QVBoxLayout(metadata_group);
	timestamp_check = new QCheckBox(tr("Дата и время в имени файла"), metadata_group);
	timestamp_check->setChecked(true);
	metadata_check = new QCheckBox(tr("Параметры фото рядом (.json)"), metadata_group);
	metadata_check->setChecked(true);
	metadata_layout->addWidget(timestamp_check);
	metadata_layout->addWidget(metadata_check);
	layout->addWidget(metadata_group);
	layout->addStretch(1);

	connect(image_format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { controlsChanged(); });
	connect(image_quality_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { controlsChanged(); });
	connect(colour_space_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { controlsChanged(); });
	connect(output_directory_edit, &QLineEdit::textChanged, this, [this](const QString&) { controlsChanged(); });
	connect(timestamp_check, &QCheckBox::toggled, this, [this](bool) { controlsChanged(); });
	connect(metadata_check, &QCheckBox::toggled, this, [this](bool) { controlsChanged(); });
	connect(browse, &QToolButton::clicked, this, [this]() {
		const QString directory = QFileDialog::getExistingDirectory(this, tr("Каталог снимков и видео"), output_directory_edit->text());
		if(!directory.isEmpty())
			output_directory_edit->setText(directory);
		emit outputDirectoryBrowseRequested();
	});
	return content;
}


void PhotoVideoSettingsPanel::setIconDirectory(const QString& directory)
{
	icon_directory = directory;
	applyIcons();
}


void PhotoVideoSettingsPanel::applyIcons()
{
	if(icon_directory.isEmpty())
		return;
	const QColor colour = palette().color(QPalette::ButtonText);
	LucideIconUtils::setButtonIcon(save_preset_button, icon_directory, QStringLiteral("save"), colour);
	LucideIconUtils::setButtonIcon(delete_preset_button, icon_directory, QStringLiteral("trash-2"), colour);
	LucideIconUtils::setButtonIcon(reset_button, icon_directory, QStringLiteral("refresh-cw"), colour);
	LucideIconUtils::setButtonIcon(gallery_button, icon_directory, QStringLiteral("folder-open"), colour);
	LucideIconUtils::setButtonIcon(capture_button, icon_directory, QStringLiteral("camera"), colour);
	LucideIconUtils::setButtonIcon(record_button, icon_directory, QStringLiteral("video"), colour);
}


QVariantMap PhotoVideoSettingsPanel::captureState() const
{
	QVariantMap state;
	state.insert(QStringLiteral("camera_mode"), camera_mode_combo->currentData().toString());
	state.insert(QStringLiteral("autofocus_mode"), autofocus_mode_combo->currentData().toString());
	state.insert(QStringLiteral("dof_blur"), dof_blur_spin->value());
	state.insert(QStringLiteral("focus_distance"), focus_distance_spin->value());
	state.insert(QStringLiteral("ev"), ev_spin->value());
	state.insert(QStringLiteral("saturation"), saturation_spin->value());
	state.insert(QStringLiteral("focal_length_mm"), focal_length_spin->value());
	state.insert(QStringLiteral("roll_degrees"), roll_spin->value());
	state.insert("bloom", bloom_spin->value());
	state.insert("warmth", warmth_spin->value());
	state.insert("tint", tint_spin->value());
	state.insert("vignette", vignette_spin->value());
	state.insert("shift_x", shift_x_spin->value());
	state.insert("shift_y", shift_y_spin->value());
	state.insert(QStringLiteral("show_grid"), grid_check->isChecked());
	state.insert(QStringLiteral("hide_world_ui"), hide_ui_check->isChecked());
	state.insert(QStringLiteral("aspect_ratio"), aspect_ratio_combo->currentData());
	state.insert(QStringLiteral("capture_delay"), capture_delay_combo->currentData());
	state.insert(QStringLiteral("image_quality"), image_quality_spin->value());
	state.insert(QStringLiteral("resolution"), resolution_combo->currentData().toString());
	state.insert(QStringLiteral("frame_rate"), frame_rate_spin->value());
	state.insert(QStringLiteral("codec"), codec_combo->currentData().toString());
	state.insert(QStringLiteral("bitrate_mbps"), bitrate_spin->value());
	state.insert(QStringLiteral("quality"), quality_combo->currentData().toString());
	state.insert(QStringLiteral("stabilisation"), stabilisation_check->isChecked());
	state.insert(QStringLiteral("microphone"), microphone_check->isChecked());
	state.insert(QStringLiteral("world_audio"), system_audio_check->isChecked());
	state.insert(QStringLiteral("maximum_duration_minutes"), maximum_duration_spin->value());
	state.insert(QStringLiteral("image_format"), image_format_combo->currentData().toString());
	state.insert(QStringLiteral("colour_space"), colour_space_combo->currentData().toString());
	state.insert(QStringLiteral("output_directory"), output_directory_edit->text());
	state.insert(QStringLiteral("timestamp_filename"), timestamp_check->isChecked());
	state.insert(QStringLiteral("camera_metadata"), metadata_check->isChecked());
	for(auto it = pro_sliders.cbegin(); it != pro_sliders.cend(); ++it)
		state.insert(it.key(), it.value()->value());
	for(auto it = pro_checks.cbegin(); it != pro_checks.cend(); ++it)
		state.insert(it.key(), it.value()->isChecked());
	state.insert(QStringLiteral("lut_path"), lut_path_edit->text());
	state.insert(QStringLiteral("photo_resolution"), photo_resolution_combo->currentData());
	return state;
}


QVariantMap PhotoVideoSettingsPanel::currentSettings() const
{
	QVariantMap state = captureState();
	state.insert(QStringLiteral("compare_original"), compare_original);
	return state;
}


QString PhotoVideoSettingsPanel::currentPresetName() const
{
	const QString name = preset_combo->currentText().trimmed();
	return name.isEmpty() ? tr("Без названия") : name;
}


void PhotoVideoSettingsPanel::restoreState(const QVariantMap& state)
{
	restoring_state = true;
	auto set_combo_data = [](QComboBox* combo, const QVariant& data) {
		const int index = combo->findData(data);
		if(index >= 0)
			combo->setCurrentIndex(index);
	};
	set_combo_data(camera_mode_combo, state.value(QStringLiteral("camera_mode"), QStringLiteral("standard")));
	const QString stored_autofocus_mode = state.value(QStringLiteral("autofocus_mode"), QStringLiteral("off")).toString();
	// CameraController supports only Off and Eye.  Treat the old MVP "point"
	// value (and any unknown value) as Off when restoring persisted presets.
	set_combo_data(autofocus_mode_combo, stored_autofocus_mode == QStringLiteral("eye") ? QStringLiteral("eye") : QStringLiteral("off"));
	dof_blur_spin->setValue(state.value(QStringLiteral("dof_blur"), 0.0).toDouble());
	focus_distance_spin->setValue(state.value(QStringLiteral("focus_distance"), 3.0).toDouble());
	ev_spin->setValue(state.value(QStringLiteral("ev"), 0.0).toDouble());
	saturation_spin->setValue(state.value(QStringLiteral("saturation"), 1.0).toDouble());
	focal_length_spin->setValue(state.value(QStringLiteral("focal_length_mm"), 25.0).toDouble());
	roll_spin->setValue(state.value(QStringLiteral("roll_degrees"), 0.0).toDouble());
	bloom_spin->setValue(state.value("bloom", 0.0).toDouble());
	warmth_spin->setValue(state.value("warmth", 0.0).toDouble());
	tint_spin->setValue(state.value("tint", 0.0).toDouble());
	vignette_spin->setValue(state.value("vignette", 0.0).toDouble());
	shift_x_spin->setValue(state.value("shift_x", 0.0).toDouble());
	shift_y_spin->setValue(state.value("shift_y", 0.0).toDouble());
	grid_check->setChecked(state.value(QStringLiteral("show_grid"), false).toBool());
	hide_ui_check->setChecked(state.value(QStringLiteral("hide_world_ui"), true).toBool());
	set_combo_data(aspect_ratio_combo, state.value(QStringLiteral("aspect_ratio"), 0.0));
	set_combo_data(capture_delay_combo, state.value(QStringLiteral("capture_delay"), 0));
	image_quality_spin->setValue(state.value(QStringLiteral("image_quality"), 95).toInt());
	set_combo_data(resolution_combo, state.value(QStringLiteral("resolution"), QStringLiteral("1920x1080")));
	frame_rate_spin->setValue(state.value(QStringLiteral("frame_rate"), 60).toInt());
	set_combo_data(codec_combo, state.value(QStringLiteral("codec"), QStringLiteral("h264")));
	bitrate_spin->setValue(state.value(QStringLiteral("bitrate_mbps"), 20).toInt());
	set_combo_data(quality_combo, state.value(QStringLiteral("quality"), QStringLiteral("high")));
	stabilisation_check->setChecked(false);
	microphone_check->setChecked(false);
	system_audio_check->setChecked(false);
	maximum_duration_spin->setValue(state.value(QStringLiteral("maximum_duration_minutes"), 0).toInt());
	set_combo_data(image_format_combo, state.value(QStringLiteral("image_format"), QStringLiteral("png")));
	set_combo_data(colour_space_combo, state.value(QStringLiteral("colour_space"), QStringLiteral("srgb")));
	output_directory_edit->setText(state.value(QStringLiteral("output_directory")).toString());
	timestamp_check->setChecked(state.value(QStringLiteral("timestamp_filename"), true).toBool());
	metadata_check->setChecked(state.value(QStringLiteral("camera_metadata"), true).toBool());
	for(auto it = pro_sliders.cbegin(); it != pro_sliders.cend(); ++it) {
		const double value = state.value(it.key(), pro_defaults.value(it.key())).toDouble();
		it.value()->setValue(std::isfinite(value) ? value : pro_defaults.value(it.key()).toDouble());
	}
	for(auto it = pro_checks.cbegin(); it != pro_checks.cend(); ++it)
		it.value()->setChecked(state.value(it.key(), false).toBool());
	lut_path_edit->setText(state.value(QStringLiteral("lut_path")).toString());
	photo_resolution_combo->setCurrentIndex(0);
	set_combo_data(photo_resolution_combo, state.value(QStringLiteral("photo_resolution"), QStringLiteral("viewport")));
	compare_original = false;
	compare_button->setDown(false);
	restoring_state = false;
	focus_distance_spin->parentWidget()->setEnabled(autofocus_mode_combo->currentData() == QStringLiteral("off"));
	image_quality_spin->setEnabled(image_format_combo->currentData() != QStringLiteral("png"));
	histogram_label->setVisible(pro_checks.value("show_histogram")->isChecked());
}


void PhotoVideoSettingsPanel::controlsChanged()
{
	if(restoring_state)
		return;
	focus_distance_spin->parentWidget()->setEnabled(autofocus_mode_combo->currentData() == QStringLiteral("off"));
	image_quality_spin->setEnabled(image_format_combo->currentData() != QStringLiteral("png"));
	histogram_label->setVisible(pro_checks.value("show_histogram")->isChecked());
	settings_save_timer.start();
	emit settingsChanged(currentSettings());
}


void PhotoVideoSettingsPanel::resetControls()
{
	QVariantMap defaults;
	defaults.insert(QStringLiteral("camera_mode"), QStringLiteral("standard"));
	defaults.insert(QStringLiteral("autofocus_mode"), QStringLiteral("off"));
	defaults.insert(QStringLiteral("dof_blur"), 0.0);
	defaults.insert(QStringLiteral("focus_distance"), 3.0);
	defaults.insert(QStringLiteral("ev"), 0.0);
	defaults.insert(QStringLiteral("saturation"), 1.0);
	defaults.insert(QStringLiteral("focal_length_mm"), 25.0);
	defaults.insert(QStringLiteral("roll_degrees"), 0.0);
	defaults.insert(QStringLiteral("show_grid"), false);
	defaults.insert(QStringLiteral("hide_world_ui"), true);
	defaults.insert(QStringLiteral("resolution"), QStringLiteral("1920x1080"));
	defaults.insert(QStringLiteral("frame_rate"), 30);
	defaults.insert(QStringLiteral("codec"), QStringLiteral("h264"));
	defaults.insert(QStringLiteral("bitrate_mbps"), 20);
	defaults.insert(QStringLiteral("quality"), QStringLiteral("high"));
	defaults.insert(QStringLiteral("stabilisation"), false);
	defaults.insert(QStringLiteral("microphone"), false);
	defaults.insert(QStringLiteral("world_audio"), false);
	defaults.insert(QStringLiteral("maximum_duration_minutes"), 0);
	defaults.insert(QStringLiteral("image_format"), QStringLiteral("png"));
	defaults.insert(QStringLiteral("colour_space"), QStringLiteral("srgb"));
	defaults.insert(QStringLiteral("timestamp_filename"), true);
	defaults.insert(QStringLiteral("camera_metadata"), true);
	restoreState(defaults);
	controlsChanged();
}


void PhotoVideoSettingsPanel::setRecording(bool recording)
{
	const QSignalBlocker blocker(record_button);
	record_button->setChecked(recording);
#ifdef _WIN32
	record_button->setEnabled(true);
	video_page->findChild<QGroupBox*>(QStringLiteral("photoVideoFormat"))->setEnabled(!recording);
	video_page->findChild<QGroupBox*>(QStringLiteral("photoVideoAudio"))->setEnabled(!recording);
#endif
	updateRecordButton();
}

void PhotoVideoSettingsPanel::setRecordingFinalising()
{
	record_button->setEnabled(false);
	record_button->setText(tr("Сохранение…"));
}


void PhotoVideoSettingsPanel::updateRecordButton()
{
	record_button->setText(record_button->isChecked() ? tr("Остановить") : tr("Запись"));
}


void PhotoVideoSettingsPanel::refreshPresets()
{
	const QString current = preset_combo->currentText();
	QStringList names;
	if(settings)
	{
		settings->beginGroup(settingsRoot() + QStringLiteral("/presets"));
		const QStringList groups = settings->childGroups();
		for(const QString& group : groups)
		{
			settings->beginGroup(group);
			const QString name = settings->value(QStringLiteral("display_name")).toString();
			settings->endGroup();
			if(!name.isEmpty() && !names.contains(name))
				names.push_back(name);
		}
		settings->endGroup();
	}
	if(!names.contains(tr("По умолчанию")))
		names.prepend(tr("По умолчанию"));
	const QSignalBlocker blocker(preset_combo);
	preset_combo->clear();
	preset_combo->addItems(names);
	const int index = preset_combo->findText(current);
	if(index >= 0)
		preset_combo->setCurrentIndex(index);
}


void PhotoVideoSettingsPanel::loadPreset(const QString& preset_name)
{
	QVariantMap state;
	if(settings)
	{
		settings->beginGroup(settingsRoot() + QStringLiteral("/presets/") + presetGroupKey(preset_name));
		state = settings->value(QStringLiteral("state")).toMap();
		settings->endGroup();
		settings->setValue(settingsRoot() + QStringLiteral("/last_preset"), preset_name);
	}
	if(state.isEmpty())
		resetControls();
	else
	{
		restoreState(state);
		controlsChanged();
	}
}


PhotoVideoSettingsPanel::~PhotoVideoSettingsPanel()
{
	if(settings)
		settings->setValue(settingsRoot() + QStringLiteral("/current_state"), captureState());
}


void PhotoVideoSettingsPanel::cancelCapture()
{
	capture_timer.stop();
	capture_seconds_remaining = 0;
	capture_button->setText(tr("Снимок"));
}


void PhotoVideoSettingsPanel::updateAutofocusDistance(double distance)
{
	if(autofocus_mode_combo->currentData() != QStringLiteral("eye")) return;
	// Update both readout and slider without applying settings or writing QSettings every frame.
	restoring_state = true;
	focus_distance_spin->setValue(distance);
	restoring_state = false;
}


void PhotoVideoSettingsPanel::setManualFocusDistance(double distance)
{
	if(!std::isfinite(distance)) return;
	restoring_state = true;
	autofocus_mode_combo->setCurrentIndex(autofocus_mode_combo->findData(QStringLiteral("off")));
	focus_distance_spin->setValue(distance);
	restoring_state = false;
	emit autofocusModeChanged(QStringLiteral("off"));
	controlsChanged();
}


void PhotoVideoSettingsPanel::setCameraMode(const QString& mode)
{
	const int index = camera_mode_combo->findData(mode);
	if(index < 0) return;
	restoring_state = true;
	camera_mode_combo->setCurrentIndex(index);
	restoring_state = false;
	emit cameraModeChanged(mode);
	controlsChanged();
}


void PhotoVideoSettingsPanel::setPathOptics(double focus, double lens_mm, double roll_degrees)
{
	if(!std::isfinite(focus) || !std::isfinite(lens_mm) || !std::isfinite(roll_degrees)) return;
	const bool was_autofocus = autofocus_mode_combo->currentData() != QStringLiteral("off");
	restoring_state = true;
	autofocus_mode_combo->setCurrentIndex(autofocus_mode_combo->findData(QStringLiteral("off")));
	focus_distance_spin->setValue(focus);
	focal_length_spin->setValue(lens_mm);
	roll_spin->setValue(roll_degrees);
	restoring_state = false;
	if(was_autofocus) emit autofocusModeChanged(QStringLiteral("off"));
	controlsChanged();
}


void PhotoVideoSettingsPanel::setHistogram(const QImage& image)
{
	if(image.isNull()) {
		histogram_label->clear();
		histogram_label->setText(tr("Гистограмма недоступна"));
		return;
	}
	histogram_label->setPixmap(QPixmap::fromImage(image).scaled(
		qMax(1, histogram_label->contentsRect().width()), histogram_label->height(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}


void PhotoVideoSettingsPanel::setTrajectoryStatus(const QString& status)
{
	trajectory_status->setTextFormat(Qt::PlainText);
	trajectory_status->setText(status);
}


void PhotoVideoSettingsPanel::setCompareOriginal(bool enabled)
{
	if(compare_original == enabled) return;
	compare_original = enabled;
	if(!enabled) compare_button->setDown(false);
	// Preview-only state: never enters current_state, presets, captures or metadata.
	emit settingsChanged(currentSettings());
}


bool PhotoVideoSettingsPanel::eventFilter(QObject* watched, QEvent* event)
{
	if(event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate ||
		(watched == compare_button && event->type() == QEvent::FocusOut))
		setCompareOriginal(false);
	return QWidget::eventFilter(watched, event);
}


void PhotoVideoSettingsPanel::saveCurrentPreset()
{
	const QString preset_name = currentPresetName();
	const QVariantMap state = captureState();
	if(settings)
	{
		settings->beginGroup(settingsRoot() + QStringLiteral("/presets/") + presetGroupKey(preset_name));
		settings->setValue(QStringLiteral("display_name"), preset_name);
		settings->setValue(QStringLiteral("state"), state);
		settings->endGroup();
		settings->setValue(settingsRoot() + QStringLiteral("/last_preset"), preset_name);
		refreshPresets();
		const int index = preset_combo->findText(preset_name);
		if(index >= 0)
			preset_combo->setCurrentIndex(index);
	}
	emit presetSaved(preset_name, state);
}


void PhotoVideoSettingsPanel::deleteCurrentPreset()
{
	const QString preset_name = currentPresetName();
	if(settings && preset_name != tr("По умолчанию"))
	{
		settings->beginGroup(settingsRoot() + QStringLiteral("/presets"));
		settings->remove(presetGroupKey(preset_name));
		settings->endGroup();
	}
	refreshPresets();
	preset_combo->setCurrentIndex(0);
	loadPreset(preset_combo->currentText());
}
