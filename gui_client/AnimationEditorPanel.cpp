/*=====================================================================
AnimationEditorPanel.cpp
------------------------
Native Qt animation-profile editor panel for Metasiberia.
=====================================================================*/


#include "AnimationEditorPanel.h"
#include "LucideIconUtils.h"


#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
#include <QtGui/QPalette>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>


namespace
{
const QString settingsRoot()
{
	return QStringLiteral("animation_editor");
}


QString profileGroupKey(const QString& profile_name)
{
	return QString::fromLatin1(profile_name.trimmed().toUtf8().toHex());
}


QToolButton* makeToolButton(QWidget* parent, const QString& tooltip, bool checkable = false)
{
	QToolButton* button = new QToolButton(parent);
	button->setAutoRaise(true);
	button->setCheckable(checkable);
	button->setToolTip(tooltip);
	button->setStatusTip(tooltip);
	return button;
}
}


AnimationEditorPanel::AnimationEditorPanel(QSettings* settings_, QWidget* parent)
:
	QWidget(parent),
	settings(settings_),
	restoring_state(false),
	preview_duration_seconds(0.0),
	profile_combo(nullptr),
	save_profile_button(nullptr),
	undo_button(nullptr),
	redo_button(nullptr),
	preview_label(nullptr),
	preview_time_label(nullptr),
	previous_button(nullptr),
	play_button(nullptr),
	next_button(nullptr),
	reset_preview_button(nullptr),
	timeline_slider(nullptr),
	tabs(nullptr),
	search_edit(nullptr),
	category_combo(nullptr),
	category_list(nullptr),
	animation_table(nullptr),
	loop_check(nullptr),
	root_motion_check(nullptr),
	mirror_check(nullptr),
	interruptible_check(nullptr),
	speed_spin(nullptr),
	blend_in_spin(nullptr),
	blend_out_spin(nullptr),
	transition_duration_spin(nullptr),
	assignment_table(nullptr),
	events_table(nullptr),
	skeleton_table(nullptr),
	import_filename_edit(nullptr),
	import_format_combo(nullptr),
	import_button(nullptr),
	add_button(nullptr),
	apply_button(nullptr),
	save_set_button(nullptr)
{
	setObjectName(QStringLiteral("animationEditorPanel"));
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	setMinimumWidth(420);

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(7);

	QHBoxLayout* profile_row = new QHBoxLayout();
	QLabel* profile_label = new QLabel(tr("Профиль:"), this);
	profile_combo = new QComboBox(this);
	profile_combo->setEditable(true);
	profile_combo->setInsertPolicy(QComboBox::NoInsert);
	profile_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	save_profile_button = makeToolButton(this, tr("Сохранить профиль анимаций"));
	undo_button = makeToolButton(this, tr("Отменить последнее изменение"));
	redo_button = makeToolButton(this, tr("Повторить отменённое изменение"));
	profile_row->addWidget(profile_label);
	profile_row->addWidget(profile_combo, 1);
	profile_row->addWidget(save_profile_button);
	profile_row->addWidget(undo_button);
	profile_row->addWidget(redo_button);
	root->addLayout(profile_row);

	QGroupBox* preview_group = new QGroupBox(tr("Предпросмотр"), this);
	QVBoxLayout* preview_layout = new QVBoxLayout(preview_group);
	preview_label = new QLabel(tr("Предпросмотр аватара подключается через MainWindow"), preview_group);
	preview_label->setObjectName(QStringLiteral("animationPreviewSurface"));
	preview_label->setAlignment(Qt::AlignCenter);
	preview_label->setMinimumHeight(145);
	preview_label->setFrameShape(QFrame::StyledPanel);
	preview_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	preview_layout->addWidget(preview_label, 1);

	QHBoxLayout* transport = new QHBoxLayout();
	previous_button = makeToolButton(preview_group, tr("Предыдущий кадр"));
	play_button = makeToolButton(preview_group, tr("Воспроизвести"), true);
	next_button = makeToolButton(preview_group, tr("Следующий кадр"));
	reset_preview_button = makeToolButton(preview_group, tr("Вернуть анимацию к началу"));
	preview_time_label = new QLabel(QStringLiteral("00:00.00 / 00:00.00"), preview_group);
	preview_time_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	transport->addWidget(previous_button);
	transport->addWidget(play_button);
	transport->addWidget(next_button);
	transport->addWidget(reset_preview_button);
	transport->addStretch(1);
	transport->addWidget(preview_time_label);
	preview_layout->addLayout(transport);

	timeline_slider = new QSlider(Qt::Horizontal, preview_group);
	timeline_slider->setRange(0, 1000);
	timeline_slider->setTracking(true);
	timeline_slider->setToolTip(tr("Позиция предпросмотра анимации"));
	preview_layout->addWidget(timeline_slider);
	root->addWidget(preview_group, 1);

	tabs = new QTabWidget(this);
	tabs->setDocumentMode(true);
	tabs->setUsesScrollButtons(true);
	tabs->addTab(makeLibraryTab(), tr("Библиотека"));
	tabs->addTab(makeSettingsTab(), tr("Настройка"));
	tabs->addTab(makeAssignmentTab(), tr("Назначение"));
	tabs->addTab(makeTransitionsTab(), tr("Переходы"));
	tabs->addTab(makeEventsTab(), tr("События"));
	tabs->addTab(makeSkeletonTab(), tr("Скелет"));
	tabs->addTab(makeImportTab(), tr("Импорт"));
	root->addWidget(tabs, 2);

	QHBoxLayout* bottom_row = new QHBoxLayout();
	import_button = new QPushButton(tr("Импортировать"), this);
	add_button = new QPushButton(tr("Добавить"), this);
	apply_button = new QPushButton(tr("Применить к аватару"), this);
	save_set_button = new QPushButton(tr("Сохранить набор"), this);
	bottom_row->addWidget(import_button);
	bottom_row->addWidget(add_button);
	bottom_row->addStretch(1);
	bottom_row->addWidget(apply_button);
	bottom_row->addWidget(save_set_button);
	root->addLayout(bottom_row);

	connect(profile_combo, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
		loadProfile(profile_combo->currentText());
	});
	connect(save_profile_button, &QToolButton::clicked, this, [this]() { saveCurrentProfile(true); });
	connect(save_set_button, &QPushButton::clicked, this, [this]() { saveCurrentProfile(true); });
	connect(undo_button, &QToolButton::clicked, this, &AnimationEditorPanel::undo);
	connect(redo_button, &QToolButton::clicked, this, &AnimationEditorPanel::redo);
	connect(previous_button, &QToolButton::clicked, this, [this]() { emit previewStepRequested(-1); });
	connect(next_button, &QToolButton::clicked, this, [this]() { emit previewStepRequested(1); });
	connect(play_button, &QToolButton::toggled, this, [this](bool playing) {
		play_button->setToolTip(playing ? tr("Пауза") : tr("Воспроизвести"));
		emit previewPlaybackChanged(playing);
	});
	connect(reset_preview_button, &QToolButton::clicked, this, [this]() {
		setPreviewPosition(0.0);
		emit previewResetRequested();
	});
	connect(timeline_slider, &QSlider::sliderMoved, this, [this](int position) {
		updateTransportText();
		emit previewSeekRequested(position / 1000.0);
	});
	connect(import_button, &QPushButton::clicked, this, [this]() { tabs->setCurrentWidget(tabs->widget(6)); });
	connect(add_button, &QPushButton::clicked, this, &AnimationEditorPanel::addAnimationRequested);
	connect(apply_button, &QPushButton::clicked, this, [this]() {
		emit applyProfileRequested(currentProfileName(), captureState());
	});

	setStyleSheet(QStringLiteral(
		"QToolButton:hover, QPushButton:hover { background: palette(highlight); color: palette(highlighted-text); }"
		"QToolButton:checked { background: palette(highlight); color: palette(highlighted-text); border: 1px solid palette(highlight); }"
		"QLabel#animationPreviewSurface { background: palette(base); }"));

	refreshProfiles();
	const QString last_profile = settings ? settings->value(settingsRoot() + QStringLiteral("/last_profile"), tr("Стандартный человек")).toString() : tr("Стандартный человек");
	const int last_index = profile_combo->findText(last_profile);
	if(last_index >= 0)
		profile_combo->setCurrentIndex(last_index);
	else
		profile_combo->setEditText(last_profile);
	loadProfile(profile_combo->currentText());
	applyIcons();
}


QWidget* AnimationEditorPanel::makeLibraryTab()
{
	QWidget* page = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(page);
	layout->setContentsMargins(5, 5, 5, 5);

	QHBoxLayout* filter_row = new QHBoxLayout();
	search_edit = new QLineEdit(page);
	search_edit->setPlaceholderText(tr("Поиск анимаций..."));
	search_edit->setClearButtonEnabled(true);
	category_combo = new QComboBox(page);
	category_combo->addItem(tr("Все категории"), QString());
	filter_row->addWidget(search_edit, 1);
	filter_row->addWidget(category_combo);
	layout->addLayout(filter_row);

	QSplitter* splitter = new QSplitter(Qt::Horizontal, page);
	category_list = new QListWidget(splitter);
	category_list->setMinimumWidth(125);
	const struct { const char* text; const char* key; } categories[] = {
		{ "Все анимации", "all" }, { "Избранные", "favourites" }, { "Недавние", "recent" },
		{ "Системные", "system" }, { "Базовый комплект", "base" }, { "Пользовательские", "user" },
		{ "Загруженные", "imported" }, { "Анимации мира", "world" }, { "Проекта", "project" }
	};
	for(const auto& category : categories)
	{
		QListWidgetItem* item = new QListWidgetItem(tr(category.text), category_list);
		item->setData(Qt::UserRole, QString::fromLatin1(category.key));
	}
	category_list->setCurrentRow(0);

	animation_table = new QTableWidget(0, 3, splitter);
	animation_table->setHorizontalHeaderLabels(QStringList() << tr("Название") << tr("Длительность") << tr("Источник"));
	animation_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	animation_table->setSelectionMode(QAbstractItemView::SingleSelection);
	animation_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	animation_table->setAlternatingRowColors(true);
	animation_table->verticalHeader()->hide();
	animation_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	animation_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	animation_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	splitter->addWidget(category_list);
	splitter->addWidget(animation_table);
	splitter->setStretchFactor(1, 1);
	layout->addWidget(splitter, 1);

	connect(search_edit, &QLineEdit::textChanged, this, [this](const QString&) { refreshAnimationTable(); });
	connect(category_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { refreshAnimationTable(); });
	connect(category_list, &QListWidget::currentRowChanged, this, [this](int) { refreshAnimationTable(); });
	connect(animation_table, &QTableWidget::itemSelectionChanged, this, [this]() {
		const QString id = selectedAnimationId();
		if(!id.isEmpty())
			emit animationSelected(id);
		controlsChanged();
	});
	return page;
}


QWidget* AnimationEditorPanel::makeSettingsTab()
{
	QWidget* page = new QWidget(this);
	QFormLayout* layout = new QFormLayout(page);
	layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	speed_spin = new QDoubleSpinBox(page);
	speed_spin->setRange(0.05, 4.0);
	speed_spin->setSingleStep(0.05);
	speed_spin->setDecimals(2);
	speed_spin->setValue(1.0);
	speed_spin->setSuffix(QStringLiteral("x"));
	blend_in_spin = new QDoubleSpinBox(page);
	blend_in_spin->setRange(0.0, 10.0);
	blend_in_spin->setValue(0.2);
	blend_in_spin->setSuffix(tr(" с"));
	blend_out_spin = new QDoubleSpinBox(page);
	blend_out_spin->setRange(0.0, 10.0);
	blend_out_spin->setValue(0.2);
	blend_out_spin->setSuffix(tr(" с"));
	loop_check = new QCheckBox(tr("Зациклить анимацию"), page);
	loop_check->setChecked(true);
	root_motion_check = new QCheckBox(tr("Использовать перемещение корня"), page);
	mirror_check = new QCheckBox(tr("Отразить анимацию по горизонтали"), page);
	layout->addRow(tr("Скорость"), speed_spin);
	layout->addRow(tr("Плавное включение"), blend_in_spin);
	layout->addRow(tr("Плавное завершение"), blend_out_spin);
	layout->addRow(QString(), loop_check);
	layout->addRow(QString(), root_motion_check);
	layout->addRow(QString(), mirror_check);

	connect(speed_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { controlsChanged(); });
	connect(blend_in_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { controlsChanged(); });
	connect(blend_out_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { controlsChanged(); });
	connect(loop_check, &QCheckBox::toggled, this, [this](bool) { controlsChanged(); });
	connect(root_motion_check, &QCheckBox::toggled, this, [this](bool) { controlsChanged(); });
	connect(mirror_check, &QCheckBox::toggled, this, [this](bool) { controlsChanged(); });
	return page;
}


QWidget* AnimationEditorPanel::makeAssignmentTab()
{
	QWidget* page = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(page);
	layout->setContentsMargins(5, 5, 5, 5);
	assignment_table = new QTableWidget(0, 2, page);
	assignment_table->setHorizontalHeaderLabels(QStringList() << tr("Назначение") << tr("Анимация"));
	assignment_table->verticalHeader()->hide();
	assignment_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	assignment_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	assignment_table->setAlternatingRowColors(true);
	const QStringList slot_names = QStringList() << tr("Ожидание") << tr("Ходьба") << tr("Бег") << tr("Прыжок")
		<< tr("Падение") << tr("Приземление") << tr("Взаимодействие") << tr("Пользовательское действие");
	assignment_table->setRowCount(slot_names.size());
	for(int row=0; row<slot_names.size(); ++row)
	{
		QTableWidgetItem* slot_item = new QTableWidgetItem(slot_names[row]);
		slot_item->setData(Qt::UserRole, QStringLiteral("slot_%1").arg(row));
		slot_item->setFlags(slot_item->flags() & ~Qt::ItemIsEditable);
		assignment_table->setItem(row, 0, slot_item);
		QComboBox* combo = new QComboBox(assignment_table);
		combo->addItem(tr("Не назначено"), QString());
		assignment_table->setCellWidget(row, 1, combo);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
        {
        });
    }
	layout->addWidget(assignment_table);
	return page;
}


QWidget* AnimationEditorPanel::makeTransitionsTab()
{
	QWidget* page = new QWidget(this);
	QFormLayout* layout = new QFormLayout(page);
	transition_duration_spin = new QDoubleSpinBox(page);
	transition_duration_spin->setRange(0.0, 10.0);
	transition_duration_spin->setSingleStep(0.05);
	transition_duration_spin->setValue(0.2);
	transition_duration_spin->setSuffix(tr(" с"));
	interruptible_check = new QCheckBox(tr("Разрешить прерывание перехода"), page);
	interruptible_check->setChecked(true);
	layout->addRow(tr("Переход по умолчанию"), transition_duration_spin);
	layout->addRow(QString(), interruptible_check);
	QLabel* note = new QLabel(tr("Точные переходы между назначениями будут переданы менеджеру анимаций через профиль."), page);
	note->setWordWrap(true);
	layout->addRow(note);
	connect(transition_duration_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { controlsChanged(); });
	connect(interruptible_check, &QCheckBox::toggled, this, [this](bool) { controlsChanged(); });
	return page;
}


QWidget* AnimationEditorPanel::makeEventsTab()
{
	QWidget* page = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(page);
	layout->setContentsMargins(5, 5, 5, 5);
	events_table = new QTableWidget(0, 3, page);
	events_table->setHorizontalHeaderLabels(QStringList() << tr("Время, с") << tr("Событие") << tr("Данные"));
	events_table->verticalHeader()->hide();
	events_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	events_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	events_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	layout->addWidget(events_table, 1);
	QHBoxLayout* buttons = new QHBoxLayout();
	QPushButton* add_event = new QPushButton(tr("Добавить событие"), page);
	QPushButton* delete_event = new QPushButton(tr("Удалить событие"), page);
	buttons->addWidget(add_event);
	buttons->addWidget(delete_event);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	connect(add_event, &QPushButton::clicked, this, [this]() {
		const int row = events_table->rowCount();
		events_table->insertRow(row);
		events_table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("0.00")));
		events_table->setItem(row, 1, new QTableWidgetItem(tr("Новое событие")));
		events_table->setItem(row, 2, new QTableWidgetItem());
		controlsChanged();
	});
	connect(delete_event, &QPushButton::clicked, this, [this]() {
		const int row = events_table->currentRow();
		if(row >= 0)
		{
			events_table->removeRow(row);
			controlsChanged();
		}
	});
	connect(events_table, &QTableWidget::cellChanged, this, [this](int, int) { controlsChanged(); });
	return page;
}


QWidget* AnimationEditorPanel::makeSkeletonTab()
{
	QWidget* page = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(page);
	layout->setContentsMargins(5, 5, 5, 5);
	QLabel* note = new QLabel(tr("Сопоставление костей исходной анимации со скелетом аватара."), page);
	note->setWordWrap(true);
	layout->addWidget(note);
	skeleton_table = new QTableWidget(0, 2, page);
	skeleton_table->setHorizontalHeaderLabels(QStringList() << tr("Кость аватара") << tr("Кость анимации"));
	skeleton_table->verticalHeader()->hide();
	skeleton_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	const QStringList bones = QStringList() << QStringLiteral("Hips") << QStringLiteral("Spine") << QStringLiteral("Spine2")
		<< QStringLiteral("Neck") << QStringLiteral("Head") << QStringLiteral("LeftShoulder") << QStringLiteral("LeftArm")
		<< QStringLiteral("LeftForeArm") << QStringLiteral("LeftHand") << QStringLiteral("RightShoulder") << QStringLiteral("RightArm")
		<< QStringLiteral("RightForeArm") << QStringLiteral("RightHand") << QStringLiteral("LeftUpLeg") << QStringLiteral("LeftLeg")
		<< QStringLiteral("LeftFoot") << QStringLiteral("RightUpLeg") << QStringLiteral("RightLeg") << QStringLiteral("RightFoot");
	skeleton_table->setRowCount(bones.size());
	for(int row=0; row<bones.size(); ++row)
	{
		QTableWidgetItem* target = new QTableWidgetItem(bones[row]);
		target->setFlags(target->flags() & ~Qt::ItemIsEditable);
		skeleton_table->setItem(row, 0, target);
		skeleton_table->setItem(row, 1, new QTableWidgetItem(bones[row]));
	}
	layout->addWidget(skeleton_table, 1);
	connect(skeleton_table, &QTableWidget::cellChanged, this, [this](int, int) { controlsChanged(); });
	return page;
}


QWidget* AnimationEditorPanel::makeImportTab()
{
	QWidget* page = new QWidget(this);
	QFormLayout* layout = new QFormLayout(page);
	QWidget* file_row_widget = new QWidget(page);
	QHBoxLayout* file_row = new QHBoxLayout(file_row_widget);
	file_row->setContentsMargins(0, 0, 0, 0);
	import_filename_edit = new QLineEdit(file_row_widget);
	import_filename_edit->setPlaceholderText(tr("Выберите файл анимации..."));
	QPushButton* browse = new QPushButton(tr("Обзор"), file_row_widget);
	file_row->addWidget(import_filename_edit, 1);
	file_row->addWidget(browse);
	import_format_combo = new QComboBox(page);
	import_format_combo->addItem(QStringLiteral("Автоматически"), QStringLiteral("auto"));
	import_format_combo->addItem(QStringLiteral("glTF / GLB"), QStringLiteral("gltf"));
	import_format_combo->addItem(QStringLiteral("FBX"), QStringLiteral("fbx"));
	import_format_combo->addItem(QStringLiteral("BVH"), QStringLiteral("bvh"));
	import_format_combo->addItem(QStringLiteral("VRM"), QStringLiteral("vrm"));
	QPushButton* import_file = new QPushButton(tr("Импортировать анимацию"), page);
	layout->addRow(tr("Файл"), file_row_widget);
	layout->addRow(tr("Формат"), import_format_combo);
	layout->addRow(QString(), import_file);
	QLabel* note = new QLabel(tr("Импорт передаёт выбранный файл подсистеме анимаций. Исходный файл не изменяется."), page);
	note->setWordWrap(true);
	layout->addRow(note);
	connect(browse, &QPushButton::clicked, this, [this]() {
		const QString filename = QFileDialog::getOpenFileName(this, tr("Импорт анимации"), QString(),
			tr("Анимации (*.glb *.gltf *.fbx *.bvh *.vrm);;Все файлы (*)"));
		if(!filename.isEmpty())
			import_filename_edit->setText(filename);
	});
	connect(import_file, &QPushButton::clicked, this, [this]() {
		if(!import_filename_edit->text().trimmed().isEmpty())
			emit animationImportRequested(import_filename_edit->text().trimmed(), import_format_combo->currentData().toString());
	});
	return page;
}


void AnimationEditorPanel::setIconDirectory(const QString& directory)
{
	icon_directory = directory;
	applyIcons();
}


void AnimationEditorPanel::applyIcons()
{
	if(icon_directory.isEmpty())
		return;
	const QColor colour = palette().color(QPalette::ButtonText);
	LucideIconUtils::setButtonIcon(save_profile_button, icon_directory, QStringLiteral("save"), colour);
	LucideIconUtils::setButtonIcon(undo_button, icon_directory, QStringLiteral("undo-2"), colour);
	LucideIconUtils::setButtonIcon(redo_button, icon_directory, QStringLiteral("redo-2"), colour);
	LucideIconUtils::setButtonIcon(previous_button, icon_directory, QStringLiteral("arrow-left"), colour);
	LucideIconUtils::setButtonIcon(play_button, icon_directory, QStringLiteral("navigation"), colour);
	LucideIconUtils::setButtonIcon(next_button, icon_directory, QStringLiteral("arrow-up"), colour);
	LucideIconUtils::setButtonIcon(reset_preview_button, icon_directory, QStringLiteral("refresh-cw"), colour);
	LucideIconUtils::setButtonIcon(import_button, icon_directory, QStringLiteral("file-input"), colour);
	LucideIconUtils::setButtonIcon(add_button, icon_directory, QStringLiteral("plus"), colour);
	LucideIconUtils::setButtonIcon(apply_button, icon_directory, QStringLiteral("circle-check"), colour);
	LucideIconUtils::setButtonIcon(save_set_button, icon_directory, QStringLiteral("save"), colour);
}


void AnimationEditorPanel::setAnimations(const QVector<AnimationEditorItem>& new_animations)
{
	const QString old_selected_id = selectedAnimationId();
	animations = new_animations;

	QStringList categories;
	for(int i=0; i<animations.size(); ++i)
		if(!animations[i].category.trimmed().isEmpty() && !categories.contains(animations[i].category))
			categories.push_back(animations[i].category);
	categories.sort(Qt::CaseInsensitive);
	{
		const QSignalBlocker blocker(category_combo);
		const QString current_category = category_combo->currentData().toString();
		category_combo->clear();
		category_combo->addItem(tr("Все категории"), QString());
		for(const QString& category : categories)
			category_combo->addItem(category, category);
		const int index = category_combo->findData(current_category);
		category_combo->setCurrentIndex(index >= 0 ? index : 0);
	}
	refreshAnimationTable();
	refreshAnimationCombos();
	if(!deferred_profile_state.isEmpty())
	{
		const QVariantMap state = deferred_profile_state;
		deferred_profile_state.clear();
		restoreState(state);
		undo_stack.clear();
		redo_stack.clear();
		pushUndoSnapshot();
	}
	if(!old_selected_id.isEmpty())
	{
		for(int row=0; row<animation_table->rowCount(); ++row)
			if(animation_table->item(row, 0)->data(Qt::UserRole).toString() == old_selected_id)
			{
				animation_table->selectRow(row);
				break;
			}
	}
}


void AnimationEditorPanel::refreshAnimationTable()
{
	if(!animation_table)
		return;
	const QString selected_id = selectedAnimationId();
	const QString search = search_edit->text().trimmed();
	const QString category_filter = category_combo->currentData().toString();
	QString sidebar_filter = QStringLiteral("all");
	if(category_list->currentItem())
		sidebar_filter = category_list->currentItem()->data(Qt::UserRole).toString();

	const QSignalBlocker blocker(animation_table);
	animation_table->setRowCount(0);
	for(int i=0; i<animations.size(); ++i)
	{
		const AnimationEditorItem& animation = animations[i];
		const QString searchable = animation.name + QLatin1Char(' ') + animation.id + QLatin1Char(' ') + animation.category + QLatin1Char(' ') + animation.source;
		if(!search.isEmpty() && !searchable.contains(search, Qt::CaseInsensitive))
			continue;
		if(!category_filter.isEmpty() && animation.category.compare(category_filter, Qt::CaseInsensitive) != 0)
			continue;
		if(sidebar_filter == QStringLiteral("favourites") && !animation.favourite)
			continue;
		if(sidebar_filter == QStringLiteral("system") && !animation.source.contains(QStringLiteral("system"), Qt::CaseInsensitive) && !animation.source.contains(tr("систем"), Qt::CaseInsensitive))
			continue;
		if(sidebar_filter == QStringLiteral("imported") && !animation.source.contains(QStringLiteral("import"), Qt::CaseInsensitive) && !animation.source.contains(tr("загруж"), Qt::CaseInsensitive))
			continue;
		if(sidebar_filter == QStringLiteral("user") && !animation.source.contains(QStringLiteral("user"), Qt::CaseInsensitive) && !animation.source.contains(tr("пользователь"), Qt::CaseInsensitive))
			continue;
		if(sidebar_filter == QStringLiteral("base") && !animation.category.contains(tr("баз"), Qt::CaseInsensitive) && !animation.category.contains(QStringLiteral("base"), Qt::CaseInsensitive))
			continue;
		if(sidebar_filter == QStringLiteral("world") && !animation.category.contains(tr("мир"), Qt::CaseInsensitive) && !animation.category.contains(QStringLiteral("world"), Qt::CaseInsensitive))
			continue;
		if(sidebar_filter == QStringLiteral("project") && !animation.category.contains(tr("проект"), Qt::CaseInsensitive) && !animation.category.contains(QStringLiteral("project"), Qt::CaseInsensitive))
			continue;

		const int row = animation_table->rowCount();
		animation_table->insertRow(row);
		QTableWidgetItem* name_item = new QTableWidgetItem(animation.favourite ? QStringLiteral("★ ") + animation.name : animation.name);
		name_item->setData(Qt::UserRole, animation.id);
		animation_table->setItem(row, 0, name_item);
		animation_table->setItem(row, 1, new QTableWidgetItem(QString::number(animation.duration_seconds, 'f', 2) + tr(" с")));
		animation_table->setItem(row, 2, new QTableWidgetItem(animation.source));
		if(animation.id == selected_id)
			animation_table->selectRow(row);
	}
}


void AnimationEditorPanel::refreshAnimationCombos()
{
	if(!assignment_table)
		return;
	for(int row=0; row<assignment_table->rowCount(); ++row)
	{
		QComboBox* combo = qobject_cast<QComboBox*>(assignment_table->cellWidget(row, 1));
		if(!combo)
			continue;
		const QString selected_id = combo->currentData().toString();
		const QSignalBlocker blocker(combo);
		combo->clear();
		combo->addItem(tr("Не назначено"), QString());
		for(int i=0; i<animations.size(); ++i)
			combo->addItem(animations[i].name, animations[i].id);
		const int index = combo->findData(selected_id);
		combo->setCurrentIndex(index >= 0 ? index : 0);
	}
}


QString AnimationEditorPanel::selectedAnimationId() const
{
	if(!animation_table || animation_table->currentRow() < 0 || !animation_table->item(animation_table->currentRow(), 0))
		return QString();
	return animation_table->item(animation_table->currentRow(), 0)->data(Qt::UserRole).toString();
}


void AnimationEditorPanel::setPreviewDuration(double duration_seconds)
{
	preview_duration_seconds = qMax(0.0, duration_seconds);
	updateTransportText();
}


void AnimationEditorPanel::setPreviewPosition(double normalised_position)
{
	const QSignalBlocker blocker(timeline_slider);
	timeline_slider->setValue(qBound(0, qRound(normalised_position * 1000.0), 1000));
	updateTransportText();
}


void AnimationEditorPanel::setPreviewStatus(const QString& status)
{
	preview_label->setText(status.isEmpty() ? tr("Предпросмотр аватара") : status);
}


void AnimationEditorPanel::updateTransportText()
{
	const double current = preview_duration_seconds * timeline_slider->value() / 1000.0;
	auto format_time = [](double seconds) {
		const int minutes = static_cast<int>(seconds) / 60;
		const double remainder = seconds - minutes * 60;
		return QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0')).arg(remainder, 5, 'f', 2, QLatin1Char('0'));
	};
	preview_time_label->setText(format_time(current) + QStringLiteral(" / ") + format_time(preview_duration_seconds));
}


QVariantMap AnimationEditorPanel::captureState() const
{
	QVariantMap state;
	state.insert(QStringLiteral("selected_animation_id"), selectedAnimationId());
	state.insert(QStringLiteral("loop"), loop_check->isChecked());
	state.insert(QStringLiteral("speed"), speed_spin->value());
	state.insert(QStringLiteral("blend_in"), blend_in_spin->value());
	state.insert(QStringLiteral("blend_out"), blend_out_spin->value());
	state.insert(QStringLiteral("root_motion"), root_motion_check->isChecked());
	state.insert(QStringLiteral("mirror"), mirror_check->isChecked());
	state.insert(QStringLiteral("transition_duration"), transition_duration_spin->value());
	state.insert(QStringLiteral("transition_interruptible"), interruptible_check->isChecked());

	QVariantMap assignments;
	for(int row=0; row<assignment_table->rowCount(); ++row)
	{
		const QComboBox* combo = qobject_cast<QComboBox*>(assignment_table->cellWidget(row, 1));
		if(combo)
			assignments.insert(assignment_table->item(row, 0)->data(Qt::UserRole).toString(), combo->currentData().toString());
	}
	state.insert(QStringLiteral("assignments"), assignments);

	QVariantList events;
	for(int row=0; row<events_table->rowCount(); ++row)
	{
		QVariantMap event;
		event.insert(QStringLiteral("time"), events_table->item(row, 0) ? events_table->item(row, 0)->text() : QString());
		event.insert(QStringLiteral("name"), events_table->item(row, 1) ? events_table->item(row, 1)->text() : QString());
		event.insert(QStringLiteral("data"), events_table->item(row, 2) ? events_table->item(row, 2)->text() : QString());
		events.push_back(event);
	}
	state.insert(QStringLiteral("events"), events);

	QVariantMap skeleton;
	for(int row=0; row<skeleton_table->rowCount(); ++row)
		if(skeleton_table->item(row, 0) && skeleton_table->item(row, 1))
			skeleton.insert(skeleton_table->item(row, 0)->text(), skeleton_table->item(row, 1)->text());
	state.insert(QStringLiteral("skeleton"), skeleton);
	return state;
}


QVariantMap AnimationEditorPanel::currentProfileState() const
{
	return captureState();
}


QString AnimationEditorPanel::currentProfileName() const
{
	const QString name = profile_combo->currentText().trimmed();
	return name.isEmpty() ? tr("Без названия") : name;
}


void AnimationEditorPanel::restoreState(const QVariantMap& state)
{
	restoring_state = true;
	{
		const QSignalBlocker b1(loop_check), b2(speed_spin), b3(blend_in_spin), b4(blend_out_spin), b5(root_motion_check), b6(mirror_check),
			b7(transition_duration_spin), b8(interruptible_check), b9(animation_table), b10(events_table), b11(skeleton_table);
		loop_check->setChecked(state.value(QStringLiteral("loop"), true).toBool());
		speed_spin->setValue(state.value(QStringLiteral("speed"), 1.0).toDouble());
		blend_in_spin->setValue(state.value(QStringLiteral("blend_in"), 0.2).toDouble());
		blend_out_spin->setValue(state.value(QStringLiteral("blend_out"), 0.2).toDouble());
		root_motion_check->setChecked(state.value(QStringLiteral("root_motion"), false).toBool());
		mirror_check->setChecked(state.value(QStringLiteral("mirror"), false).toBool());
		transition_duration_spin->setValue(state.value(QStringLiteral("transition_duration"), 0.2).toDouble());
		interruptible_check->setChecked(state.value(QStringLiteral("transition_interruptible"), true).toBool());

		const QVariantMap assignments = state.value(QStringLiteral("assignments")).toMap();
		for(int row=0; row<assignment_table->rowCount(); ++row)
		{
			QComboBox* combo = qobject_cast<QComboBox*>(assignment_table->cellWidget(row, 1));
			if(!combo)
				continue;
			const QSignalBlocker combo_blocker(combo);
			const int index = combo->findData(assignments.value(assignment_table->item(row, 0)->data(Qt::UserRole).toString()).toString());
			combo->setCurrentIndex(index >= 0 ? index : 0);
		}

		events_table->setRowCount(0);
		const QVariantList events = state.value(QStringLiteral("events")).toList();
		for(int row=0; row<events.size(); ++row)
		{
			const QVariantMap event = events[row].toMap();
			events_table->insertRow(row);
			events_table->setItem(row, 0, new QTableWidgetItem(event.value(QStringLiteral("time")).toString()));
			events_table->setItem(row, 1, new QTableWidgetItem(event.value(QStringLiteral("name")).toString()));
			events_table->setItem(row, 2, new QTableWidgetItem(event.value(QStringLiteral("data")).toString()));
		}

		const QVariantMap skeleton = state.value(QStringLiteral("skeleton")).toMap();
		for(int row=0; row<skeleton_table->rowCount(); ++row)
		{
			const QString target = skeleton_table->item(row, 0)->text();
			if(skeleton.contains(target))
				skeleton_table->item(row, 1)->setText(skeleton.value(target).toString());
		}

		const QString selected_id = state.value(QStringLiteral("selected_animation_id")).toString();
		for(int row=0; row<animation_table->rowCount(); ++row)
			if(animation_table->item(row, 0)->data(Qt::UserRole).toString() == selected_id)
			{
				animation_table->selectRow(row);
				break;
			}
	}
	restoring_state = false;
}


void AnimationEditorPanel::controlsChanged()
{
	if(restoring_state)
		return;
	pushUndoSnapshot();
	const QVariantMap state = captureState();
	emit settingsChanged(state);
}


void AnimationEditorPanel::pushUndoSnapshot()
{
	const QVariantMap state = captureState();
	if(!undo_stack.isEmpty() && undo_stack.back() == state)
		return;
	undo_stack.push_back(state);
	if(undo_stack.size() > 64)
		undo_stack.remove(0);
	redo_stack.clear();
	updateUndoButtons();
}


void AnimationEditorPanel::undo()
{
	if(undo_stack.size() < 2)
		return;
	redo_stack.push_back(undo_stack.takeLast());
	restoreState(undo_stack.back());
	updateUndoButtons();
	emit settingsChanged(captureState());
}


void AnimationEditorPanel::redo()
{
	if(redo_stack.isEmpty())
		return;
	const QVariantMap state = redo_stack.takeLast();
	restoreState(state);
	undo_stack.push_back(state);
	updateUndoButtons();
	emit settingsChanged(captureState());
}


void AnimationEditorPanel::updateUndoButtons()
{
	undo_button->setEnabled(undo_stack.size() > 1);
	redo_button->setEnabled(!redo_stack.isEmpty());
}


void AnimationEditorPanel::refreshProfiles()
{
	const QString current = profile_combo->currentText();
	QStringList names;
	if(settings)
	{
		settings->beginGroup(settingsRoot() + QStringLiteral("/profiles"));
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
	if(names.isEmpty())
		names.push_back(tr("Стандартный человек"));
	names.sort(Qt::CaseInsensitive);
	const QSignalBlocker blocker(profile_combo);
	profile_combo->clear();
	profile_combo->addItems(names);
	const int current_index = profile_combo->findText(current);
	if(current_index >= 0)
		profile_combo->setCurrentIndex(current_index);
}


void AnimationEditorPanel::loadProfile(const QString& profile_name)
{
	QVariantMap state;
	if(settings)
	{
		settings->beginGroup(settingsRoot() + QStringLiteral("/profiles/") + profileGroupKey(profile_name));
		state = settings->value(QStringLiteral("state")).toMap();
		settings->endGroup();
		settings->setValue(settingsRoot() + QStringLiteral("/last_profile"), profile_name);
	}
	if(!state.isEmpty())
	{
		restoreState(state);
		if(animations.isEmpty())
			deferred_profile_state = state;
		else
			deferred_profile_state.clear();
	}
	undo_stack.clear();
	redo_stack.clear();
	pushUndoSnapshot();
	emit profileChanged(profile_name, captureState());
}


void AnimationEditorPanel::saveCurrentProfile(bool emit_signal)
{
	const QString profile_name = currentProfileName();
	QVariantMap state = captureState();
	if(animations.isEmpty() && !deferred_profile_state.isEmpty())
		state.insert(QStringLiteral("assignments"), deferred_profile_state.value(QStringLiteral("assignments")));
	if(settings)
	{
		settings->beginGroup(settingsRoot() + QStringLiteral("/profiles/") + profileGroupKey(profile_name));
		settings->setValue(QStringLiteral("display_name"), profile_name);
		settings->setValue(QStringLiteral("state"), state);
		settings->endGroup();
		settings->setValue(settingsRoot() + QStringLiteral("/last_profile"), profile_name);
		refreshProfiles();
		const int index = profile_combo->findText(profile_name);
		if(index >= 0)
			profile_combo->setCurrentIndex(index);
	}
	if(emit_signal)
		emit profileSaved(profile_name, state);
}
