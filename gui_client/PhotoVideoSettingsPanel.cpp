/*=====================================================================
PhotoVideoSettingsPanel.cpp
---------------------------
Native Qt photo and video settings panel for Metasiberia.
=====================================================================*/


#include "PhotoVideoSettingsPanel.h"
#include "LucideIconUtils.h"


#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtGui/QPalette>
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
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>


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
	restoring_state(false),
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
	preset_combo->setEditable(true);
	preset_combo->setInsertPolicy(QComboBox::NoInsert);
	preset_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	save_preset_button = makeToolButton(this, tr("Сохранить пресет фото и видео"));
	delete_preset_button = makeToolButton(this, tr("Удалить выбранный пресет"));
	preset_row->addWidget(preset_combo, 1);
	preset_row->addWidget(save_preset_button);
	preset_row->addWidget(delete_preset_button);
	root->addLayout(preset_row);

	tabs = new QTabWidget(this);
	tabs->setDocumentMode(true);
	tabs->addTab(makeCameraTab(), tr("Камера"));
	tabs->addTab(makeVideoTab(), tr("Видео"));
	tabs->addTab(makeOutputTab(), tr("Вывод"));
	root->addWidget(tabs, 1);

	QHBoxLayout* bottom_row = new QHBoxLayout();
	reset_button = new QPushButton(tr("Сбросить"), this);
	gallery_button = new QPushButton(tr("Галерея"), this);
	capture_button = new QPushButton(tr("Снимок"), this);
	record_button = new QPushButton(this);
	record_button->setCheckable(true);
	record_button->setToolTip(tr("Начать или остановить видеозапись"));
	bottom_row->addWidget(reset_button);
	bottom_row->addWidget(gallery_button);
	bottom_row->addStretch(1);
	bottom_row->addWidget(capture_button);
	bottom_row->addWidget(record_button);
	root->addLayout(bottom_row);

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
		resetControls();
		emit resetRequested(captureState());
	});
	connect(gallery_button, &QPushButton::clicked, this, &PhotoVideoSettingsPanel::browseGalleryRequested);
	connect(capture_button, &QPushButton::clicked, this, [this]() { emit capturePhotoRequested(captureState()); });
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
		settings->beginGroup(settingsRoot() + QStringLiteral("/presets/") + presetGroupKey(last_preset));
		initial_state = settings->value(QStringLiteral("state")).toMap();
		settings->endGroup();
		if(initial_state.isEmpty())
			initial_state = settings->value(settingsRoot() + QStringLiteral("/current_state")).toMap();
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

	QGroupBox* camera_group = new QGroupBox(tr("Режим камеры"), content);
	QFormLayout* camera_layout = new QFormLayout(camera_group);
	camera_mode_combo = new QComboBox(camera_group);
	camera_mode_combo->addItem(tr("Стандартная камера"), QStringLiteral("standard"));
	camera_mode_combo->addItem(tr("Селфи-камера"), QStringLiteral("selfie"));
	camera_mode_combo->addItem(tr("Фиксированный угол"), QStringLiteral("fixed_angle"));
	camera_mode_combo->addItem(tr("Свободная камера"), QStringLiteral("free"));
	camera_mode_combo->addItem(tr("Камера слежения"), QStringLiteral("tracking"));
	autofocus_mode_combo = new QComboBox(camera_group);
	autofocus_mode_combo->addItem(tr("Выключен"), QStringLiteral("off"));
	autofocus_mode_combo->addItem(tr("По глазам"), QStringLiteral("eye"));
	camera_layout->addRow(tr("Камера"), camera_mode_combo);
	camera_layout->addRow(tr("Автофокус"), autofocus_mode_combo);
	content_layout->addWidget(camera_group);

	QGroupBox* optics_group = new QGroupBox(tr("Оптика и изображение"), content);
	QVBoxLayout* optics_layout = new QVBoxLayout(optics_group);
	optics_layout->addWidget(makeSliderRow(tr("Размытие глубины резкости"), dof_blur_spin, 0.0, 1.0, 0.01, 2, QString()));
	optics_layout->addWidget(makeSliderRow(tr("Дистанция фокуса"), focus_distance_spin, 0.1, 100.0, 0.1, 1, tr(" м")));
	optics_layout->addWidget(makeSliderRow(tr("Экспозиция (EV)"), ev_spin, -5.0, 5.0, 0.1, 1, QString()));
	optics_layout->addWidget(makeSliderRow(tr("Насыщенность"), saturation_spin, 0.0, 2.0, 0.05, 2, QString()));
	optics_layout->addWidget(makeSliderRow(tr("Фокусное расстояние"), focal_length_spin, 12.0, 200.0, 1.0, 0, tr(" мм")));
	optics_layout->addWidget(makeSliderRow(tr("Наклон камеры"), roll_spin, -180.0, 180.0, 1.0, 1, QStringLiteral("°")));
	content_layout->addWidget(optics_group);

	QGroupBox* overlay_group = new QGroupBox(tr("Интерфейс"), content);
	QVBoxLayout* overlay_layout = new QVBoxLayout(overlay_group);
	grid_check = new QCheckBox(tr("Показывать композиционную сетку"), overlay_group);
	hide_ui_check = new QCheckBox(tr("Скрывать интерфейс мира при съёмке"), overlay_group);
	hide_ui_check->setChecked(true);
	overlay_layout->addWidget(grid_check);
	overlay_layout->addWidget(hide_ui_check);
	content_layout->addWidget(overlay_group);
	content_layout->addStretch(1);

	QScrollArea* scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(content);

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
	return scroll;
}


QWidget* PhotoVideoSettingsPanel::makeSliderRow(const QString& label, QDoubleSpinBox*& spin,
	double minimum, double maximum, double step, int decimals, const QString& suffix)
{
	QWidget* row = new QWidget(this);
	QHBoxLayout* layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	QLabel* value_label = new QLabel(label, row);
	value_label->setMinimumWidth(145);
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
	if(minimum <= 0.0 && maximum >= 0.0)
		spin->setValue(0.0);
	else
		spin->setValue(minimum);
	slider->setValue(qRound(spin->value() * multiplier));
	layout->addWidget(value_label);
	layout->addWidget(slider, 1);
	layout->addWidget(spin);

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


QWidget* PhotoVideoSettingsPanel::makeVideoTab()
{
	QWidget* content = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(content);
	layout->setContentsMargins(5, 5, 5, 5);

	QGroupBox* format_group = new QGroupBox(tr("Параметры записи"), content);
	QFormLayout* format_layout = new QFormLayout(format_group);
	resolution_combo = new QComboBox(format_group);
	resolution_combo->addItem(QStringLiteral("1280 × 720"), QStringLiteral("1280x720"));
	resolution_combo->addItem(QStringLiteral("1920 × 1080"), QStringLiteral("1920x1080"));
	resolution_combo->addItem(QStringLiteral("2560 × 1440"), QStringLiteral("2560x1440"));
	resolution_combo->addItem(QStringLiteral("3840 × 2160"), QStringLiteral("3840x2160"));
	resolution_combo->setCurrentIndex(1);
	frame_rate_spin = new QSpinBox(format_group);
	frame_rate_spin->setRange(15, 240);
	frame_rate_spin->setValue(60);
	frame_rate_spin->setSuffix(QStringLiteral(" fps"));
	codec_combo = new QComboBox(format_group);
	codec_combo->addItem(QStringLiteral("H.264"), QStringLiteral("h264"));
	codec_combo->addItem(QStringLiteral("H.265 / HEVC"), QStringLiteral("hevc"));
	codec_combo->addItem(QStringLiteral("VP9"), QStringLiteral("vp9"));
	codec_combo->addItem(QStringLiteral("AV1"), QStringLiteral("av1"));
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
	maximum_duration_spin = new QSpinBox(format_group);
	maximum_duration_spin->setRange(0, 1440);
	maximum_duration_spin->setSpecialValueText(tr("Без ограничения"));
	maximum_duration_spin->setSuffix(tr(" мин"));
	format_layout->addRow(tr("Разрешение"), resolution_combo);
	format_layout->addRow(tr("Частота кадров"), frame_rate_spin);
	format_layout->addRow(tr("Кодек"), codec_combo);
	format_layout->addRow(tr("Битрейт"), bitrate_spin);
	format_layout->addRow(tr("Качество"), quality_combo);
	format_layout->addRow(tr("Максимальная длительность"), maximum_duration_spin);
	layout->addWidget(format_group);

	QGroupBox* options_group = new QGroupBox(tr("Видео и звук"), content);
	QVBoxLayout* options_layout = new QVBoxLayout(options_group);
	stabilisation_check = new QCheckBox(tr("Стабилизация камеры"), options_group);
	microphone_check = new QCheckBox(tr("Записывать микрофон"), options_group);
	system_audio_check = new QCheckBox(tr("Записывать звук мира"), options_group);
	system_audio_check->setChecked(true);
	options_layout->addWidget(stabilisation_check);
	options_layout->addWidget(microphone_check);
	options_layout->addWidget(system_audio_check);
	layout->addWidget(options_group);
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
	image_format_combo = new QComboBox(output_group);
	image_format_combo->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
	image_format_combo->addItem(QStringLiteral("JPEG"), QStringLiteral("jpeg"));
	image_format_combo->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
	colour_space_combo = new QComboBox(output_group);
	colour_space_combo->addItem(QStringLiteral("sRGB"), QStringLiteral("srgb"));
	colour_space_combo->addItem(QStringLiteral("Display P3"), QStringLiteral("display_p3"));
	colour_space_combo->addItem(QStringLiteral("Linear"), QStringLiteral("linear"));
	QWidget* directory_widget = new QWidget(output_group);
	QHBoxLayout* directory_layout = new QHBoxLayout(directory_widget);
	directory_layout->setContentsMargins(0, 0, 0, 0);
	output_directory_edit = new QLineEdit(directory_widget);
	output_directory_edit->setPlaceholderText(tr("Каталог снимков и видео"));
	QToolButton* browse = makeToolButton(directory_widget, tr("Выбрать каталог"));
	directory_layout->addWidget(output_directory_edit, 1);
	directory_layout->addWidget(browse);
	output_layout->addRow(tr("Формат снимка"), image_format_combo);
	output_layout->addRow(tr("Цветовое пространство"), colour_space_combo);
	output_layout->addRow(tr("Каталог"), directory_widget);
	layout->addWidget(output_group);

	QGroupBox* metadata_group = new QGroupBox(tr("Дополнительные данные"), content);
	QVBoxLayout* metadata_layout = new QVBoxLayout(metadata_group);
	timestamp_check = new QCheckBox(tr("Добавлять дату и время в имя файла"), metadata_group);
	timestamp_check->setChecked(true);
	metadata_check = new QCheckBox(tr("Сохранять параметры камеры в метаданных"), metadata_group);
	metadata_check->setChecked(true);
	metadata_layout->addWidget(timestamp_check);
	metadata_layout->addWidget(metadata_check);
	layout->addWidget(metadata_group);
	layout->addStretch(1);

	connect(image_format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { controlsChanged(); });
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
	state.insert(QStringLiteral("show_grid"), grid_check->isChecked());
	state.insert(QStringLiteral("hide_world_ui"), hide_ui_check->isChecked());
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
	return state;
}


QVariantMap PhotoVideoSettingsPanel::currentSettings() const
{
	return captureState();
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
	grid_check->setChecked(state.value(QStringLiteral("show_grid"), false).toBool());
	hide_ui_check->setChecked(state.value(QStringLiteral("hide_world_ui"), true).toBool());
	set_combo_data(resolution_combo, state.value(QStringLiteral("resolution"), QStringLiteral("1920x1080")));
	frame_rate_spin->setValue(state.value(QStringLiteral("frame_rate"), 60).toInt());
	set_combo_data(codec_combo, state.value(QStringLiteral("codec"), QStringLiteral("h264")));
	bitrate_spin->setValue(state.value(QStringLiteral("bitrate_mbps"), 20).toInt());
	set_combo_data(quality_combo, state.value(QStringLiteral("quality"), QStringLiteral("high")));
	stabilisation_check->setChecked(state.value(QStringLiteral("stabilisation"), false).toBool());
	microphone_check->setChecked(state.value(QStringLiteral("microphone"), false).toBool());
	system_audio_check->setChecked(state.value(QStringLiteral("world_audio"), true).toBool());
	maximum_duration_spin->setValue(state.value(QStringLiteral("maximum_duration_minutes"), 0).toInt());
	set_combo_data(image_format_combo, state.value(QStringLiteral("image_format"), QStringLiteral("png")));
	set_combo_data(colour_space_combo, state.value(QStringLiteral("colour_space"), QStringLiteral("srgb")));
	output_directory_edit->setText(state.value(QStringLiteral("output_directory")).toString());
	timestamp_check->setChecked(state.value(QStringLiteral("timestamp_filename"), true).toBool());
	metadata_check->setChecked(state.value(QStringLiteral("camera_metadata"), true).toBool());
	restoring_state = false;
}


void PhotoVideoSettingsPanel::controlsChanged()
{
	if(restoring_state)
		return;
	settings_save_timer.start();
	emit settingsChanged(captureState());
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
	defaults.insert(QStringLiteral("frame_rate"), 60);
	defaults.insert(QStringLiteral("codec"), QStringLiteral("h264"));
	defaults.insert(QStringLiteral("bitrate_mbps"), 20);
	defaults.insert(QStringLiteral("quality"), QStringLiteral("high"));
	defaults.insert(QStringLiteral("stabilisation"), false);
	defaults.insert(QStringLiteral("microphone"), false);
	defaults.insert(QStringLiteral("world_audio"), true);
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
	updateRecordButton();
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
		emit settingsChanged(captureState());
	}
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
