#include "AnimationEditorPanel.h"
#include "LucideIconUtils.h"
#include <QtCore/QSignalBlocker>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

AnimationEditorPanel::AnimationEditorPanel(QSettings*, QWidget* parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("animationEditorPanel"));
	setMinimumWidth(380);
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	auto* splitter = new QSplitter(Qt::Vertical, this);
	splitter->setObjectName(QStringLiteral("animationEditorSplitter"));
	splitter->setChildrenCollapsible(false);
	splitter->setHandleWidth(7);
	splitter->setStyleSheet(QStringLiteral(
		"QSplitter#animationEditorSplitter::handle:vertical { background: palette(mid); }"
		"QSplitter#animationEditorSplitter::handle:vertical:hover { background: palette(highlight); }"));
	root->addWidget(splitter);
	auto* preview_group = new QGroupBox(tr("Предпросмотр аватара"), this);
	preview_layout = new QVBoxLayout(preview_group);
	status_label = new QLabel(tr("Выберите жест. ЛКМ — вращение, колесо — масштаб."), preview_group);
	status_label->setWordWrap(true);
	status_label->setObjectName(QStringLiteral("animationStatus"));
	preview_layout->addWidget(status_label);
	auto* transport = new QHBoxLayout();
	auto button = [this](const QString& text, const char* name, const QString& tooltip) {
		auto* b = new QPushButton(text, this);
		b->setObjectName(QString::fromLatin1(name));
		b->setToolTip(tooltip);
		return b;
	};
	previous = button(tr("− кадр"), "animationPrevious", tr("Перейти к предыдущему кадру предпросмотра."));
	play = button(tr("Пуск"), "animationPlay", tr("Воспроизвести анимацию в предпросмотре."));
	play->setCheckable(true);
	next = button(tr("+ кадр"), "animationNext", tr("Перейти к следующему кадру предпросмотра."));
	rewind = button(tr("В начало"), "animationRewind", tr("Вернуть предпросмотр к началу анимации."));
	for(auto* b : {previous, play, next, rewind}) transport->addWidget(b);
	preview_layout->addLayout(transport);
	timeline = new QSlider(Qt::Horizontal, this);
	timeline->setObjectName(QStringLiteral("animationTimeline"));
	timeline->setRange(0, 10000);
	timeline->setToolTip(tr("Перетащите ползунок, чтобы выбрать момент анимации для предпросмотра."));
	preview_layout->addWidget(timeline);
	auto* timing = new QHBoxLayout();
	time_label = new QLabel(this);
	timing->addWidget(time_label, 1);
	timing->addWidget(new QLabel(tr("Скорость:"), this));
	speed = new QDoubleSpinBox(this);
	speed->setObjectName(QStringLiteral("animationSpeed"));
	speed->setRange(0.05, 4);
	speed->setSingleStep(0.05);
	speed->setValue(1);
	speed->setSuffix(QStringLiteral("×"));
	speed->setToolTip(tr("Скорость предпросмотра. «Сохранить копию» записывает её в новую анимацию."));
	timing->addWidget(speed);
	preview_layout->addLayout(timing);
	splitter->addWidget(preview_group);
	auto* library = new QWidget(this);
	library->setObjectName(QStringLiteral("animationLibraryPane"));
	library->setMinimumHeight(280);
	auto* library_layout = new QVBoxLayout(library);
	library_layout->setContentsMargins(0, 0, 0, 0);
	splitter->addWidget(library);
	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({360, 400});
	splitter->handle(1)->setCursor(Qt::SplitVCursor);
	splitter->handle(1)->setToolTip(tr("Перетащите вверх или вниз, чтобы изменить размеры предпросмотра и библиотеки жестов."));
	search = new QLineEdit(this);
	search->setObjectName(QStringLiteral("animationSearch"));
	search->setPlaceholderText(tr("Поиск жестов…"));
	search->setToolTip(tr("Поиск жестов по названию без учёта регистра."));
	search->setClearButtonEnabled(true);
	for(auto* clear_button : search->findChildren<QToolButton*>())
		clear_button->setToolTip(tr("Очистить поиск жестов."));
	library_layout->addWidget(search);
	table = new QTableWidget(0, 2, this);
	table->setObjectName(QStringLiteral("animationLibrary"));
	table->setHorizontalHeaderLabels({tr("Жест"), tr("Длительность")});
	table->setToolTip(tr("Выберите жест для предпросмотра и редактирования."));
	table->horizontalHeaderItem(0)->setToolTip(tr("Название жеста. Выберите строку для предпросмотра."));
	table->horizontalHeaderItem(1)->setToolTip(tr("Длительность анимации в секундах."));
	table->verticalScrollBar()->setToolTip(tr("Прокрутить список жестов вверх или вниз."));
	table->horizontalScrollBar()->setToolTip(tr("Прокрутить список жестов влево или вправо."));
	table->setMinimumHeight(100);
	table->verticalHeader()->hide();
	table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setAlternatingRowColors(true);
	library_layout->addWidget(table, 1);
	auto* flags = new QHBoxLayout();
	loop = new QCheckBox(tr("Повтор"), this);
	loop->setObjectName(QStringLiteral("animationLoop"));
	loop->setToolTip(tr("Повторять выбранный жест в предпросмотре и при исполнении в мире."));
	animate_head = new QCheckBox(tr("Анимировать голову"), this);
	animate_head->setObjectName(QStringLiteral("animationAnimateHead"));
	animate_head->setToolTip(tr("Во время жеста в мире заменять процедурный поворот головы анимацией."));
	flags->addWidget(loop);
	flags->addWidget(animate_head);
	library_layout->addLayout(flags);
	auto* copy_row = new QHBoxLayout();
	copy_name = new QLineEdit(this);
	copy_name->setObjectName(QStringLiteral("animationCopyName"));
	copy_name->setPlaceholderText(tr("Название новой копии"));
	copy_name->setToolTip(tr("Введите название, под которым будет сохранена копия выбранного жеста."));
	copy_name->setMaxLength(160);
	save_copy = button(tr("Сохранить копию"), "animationSaveCopy", tr("Создать новый жест с указанным названием и выбранной скоростью анимации."));
	copy_row->addWidget(copy_name, 1);
	copy_row->addWidget(save_copy);
	library_layout->addLayout(copy_row);
	auto* library_buttons = new QHBoxLayout();
	import_button = button(tr("Импорт в жесты…"), "animationImport", tr("Импортировать анимацию из FBX, GLB или SUBANIM и добавить её в жесты."));
	export_button = button(tr("Экспорт…"), "animationExport", tr("Сохранить выбранный жест в файл SUBANIM."));
	remove_button = button(tr("Удалить"), "animationRemove", tr("Удалить выбранный жест из библиотеки."));
	library_buttons->addWidget(import_button, 1);
	library_buttons->addWidget(export_button);
	library_buttons->addWidget(remove_button);
	library_layout->addLayout(library_buttons);
	auto* world_buttons = new QHBoxLayout();
	perform_button = button(tr("Исполнить в мире"), "animationPerform", tr("Исполнить выбранный жест вашим аватаром в мире."));
	stop_button = button(tr("Остановить жест"), "animationStop", tr("Остановить текущий жест вашего аватара в мире."));
	world_buttons->addWidget(perform_button);
	world_buttons->addWidget(stop_button);
	library_layout->addLayout(world_buttons);
	auto* help = new QLabel(tr("FBX / GLB / SUBANIM. Один скелет человека и одна анимация в файле. "
		"Импорт создаёт ресурс .subanim и добавляет кнопку в «Жесты». Исходник не изменяется."), this);
	help->setWordWrap(true);
	help->setObjectName(QStringLiteral("animationImportHelp"));
	library_layout->addWidget(help);
	connect(search, &QLineEdit::textChanged, this, [this](const QString& text) {
		for(int row = 0; row < table->rowCount(); ++row)
			table->setRowHidden(row, !table->item(row, 0)->text().contains(text, Qt::CaseInsensitive));
	});
	connect(table, &QTableWidget::itemSelectionChanged, this, &AnimationEditorPanel::selectionChanged);
	connect(play, &QPushButton::toggled, this, [this](bool playing) {
		setPreviewPlaying(playing);
		emit previewPlaybackChanged(playing);
	});
	connect(timeline, &QSlider::valueChanged, this, [this](int value) {
		updateTime();
		emit previewSeekRequested(value / 10000.0);
	});
	connect(previous, &QPushButton::clicked, this, [this]() { emit previewStepRequested(-1); });
	connect(next, &QPushButton::clicked, this, [this]() { emit previewStepRequested(1); });
	connect(rewind, &QPushButton::clicked, this, [this]() { emit previewSeekRequested(0); });
	connect(speed, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &AnimationEditorPanel::previewSpeedChanged);
	connect(loop, &QCheckBox::toggled, this, [this](bool value) {
		for(auto& item : animations) if(item.id == selectedAnimationId()) item.loop = value;
		emit previewLoopChanged(value);
		emit flagsEdited(selectedAnimationId(), value, animate_head->isChecked());
	});
	connect(animate_head, &QCheckBox::toggled, this, [this](bool value) {
		for(auto& item : animations) if(item.id == selectedAnimationId()) item.animate_head = value;
		emit flagsEdited(selectedAnimationId(), loop->isChecked(), value);
	});
	connect(import_button, &QPushButton::clicked, this, &AnimationEditorPanel::importRequested);
	connect(save_copy, &QPushButton::clicked, this, [this]() { emit saveCopyRequested(selectedAnimationId(), copy_name->text().trimmed(), speed->value()); });
	connect(remove_button, &QPushButton::clicked, this, [this]() { emit removeRequested(selectedAnimationId()); });
	connect(perform_button, &QPushButton::clicked, this, [this]() { emit performRequested(selectedAnimationId()); });
	connect(stop_button, &QPushButton::clicked, this, &AnimationEditorPanel::stopRequested);
	connect(export_button, &QPushButton::clicked, this, [this]() { emit exportRequested(selectedAnimationId()); });
	updateTime();
	updateActions();
}

void AnimationEditorPanel::setIconDirectory(const QString& directory)
{
	icon_directory = directory;
	const QColor c = palette().color(QPalette::ButtonText);
	LucideIconUtils::setButtonIcon(import_button, directory, QStringLiteral("file-input"), c);
	LucideIconUtils::setButtonIcon(save_copy, directory, QStringLiteral("save"), c);
	LucideIconUtils::setButtonIcon(remove_button, directory, QStringLiteral("trash-2"), c);
	setPreviewPlaying(play->isChecked());
}

void AnimationEditorPanel::setAnimations(const QVector<AnimationEditorItem>& items, const QString& select_id)
{
	const QString selected = select_id.isEmpty() ? selectedAnimationId() : select_id;
	animations = items;
	{
		const QSignalBlocker block(table);
		table->setRowCount(0);
		int selected_row = -1;
		for(const auto& item : items)
		{
			const int row = table->rowCount();
			table->insertRow(row);
			auto* name = new QTableWidgetItem(item.name);
			name->setData(Qt::UserRole, item.id);
			name->setToolTip(tr("Выбрать жест «%1» для предпросмотра.\nРесурс: %2").arg(item.name, item.id));
			table->setItem(row, 0, name);
			table->setItem(row, 1, new QTableWidgetItem(item.duration_seconds > 0 ? QString::number(item.duration_seconds, 'f', 2) + tr(" с") : QStringLiteral("—")));
			table->setRowHidden(row, !item.name.contains(search->text(), Qt::CaseInsensitive));
			if(item.id == selected) selected_row = row;
		}
		if(selected_row < 0)
			for(int row = 0; row < table->rowCount(); ++row)
				if(!table->isRowHidden(row)) { selected_row = row; break; }
		if(selected_row >= 0) table->selectRow(selected_row);
	}
	selectionChanged();
}

QString AnimationEditorPanel::selectedAnimationId() const
{
	const auto* item = table->item(table->currentRow(), 0);
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void AnimationEditorPanel::selectionChanged()
{
	const QString id = selectedAnimationId();
	setPreviewPlaying(false);
	emit previewPlaybackChanged(false);
	setPreviewDuration(0);
	setPreviewPosition(0);
	for(const auto& item : animations)
		if(item.id == id)
		{
			const QSignalBlocker b1(loop), b2(animate_head);
			loop->setChecked(item.loop);
			animate_head->setChecked(item.animate_head);
			copy_name->setText(item.name + tr(" — копия"));
			break;
		}
	speed->setValue(1);
	emit previewLoopChanged(loop->isChecked());
	emit animationSelected(id);
	updateActions();
}

void AnimationEditorPanel::setPreviewWidget(QWidget* widget)
{
	widget->setMinimumHeight(200);
	widget->setToolTip(tr("Левая кнопка мыши — вращение аватара; колесо — масштаб предпросмотра."));
	preview_layout->insertWidget(0, widget, 1);
}

void AnimationEditorPanel::setPreviewDuration(double seconds)
{
	duration = qMax(0.0, seconds);
	if(duration > 0 && table->currentRow() >= 0)
		table->item(table->currentRow(), 1)->setText(QString::number(duration, 'f', 2) + tr(" с"));
	updateTime();
	updateActions();
}

void AnimationEditorPanel::setPreviewPosition(double position)
{
	const QSignalBlocker block(timeline);
	if(!timeline->isSliderDown()) timeline->setValue(qRound(qBound(0.0, position, 1.0) * 10000));
	updateTime();
}

void AnimationEditorPanel::setPreviewPlaying(bool playing)
{
	const QSignalBlocker block(play);
	play->setChecked(playing);
	play->setText(playing ? tr("Пауза") : tr("Пуск"));
	play->setToolTip(playing ? tr("Приостановить анимацию в предпросмотре.") : tr("Воспроизвести анимацию в предпросмотре."));
	LucideIconUtils::setButtonIcon(play, icon_directory, playing ? QStringLiteral("pause") : QStringLiteral("play"), palette().color(QPalette::ButtonText));
}

void AnimationEditorPanel::setPreviewStatus(const QString& status) { status_label->setText(status); }
void AnimationEditorPanel::setEditingEnabled(bool enabled) { can_edit = enabled; updateActions(); }
void AnimationEditorPanel::setBusy(bool value) { busy = value; updateActions(); }

void AnimationEditorPanel::updateActions()
{
	const bool selected = !selectedAnimationId().isEmpty() && !busy;
	for(auto* b : {play, previous, next, rewind}) b->setEnabled(selected && duration > 0);
	timeline->setEnabled(selected && duration > 0);
	speed->setEnabled(selected && duration > 0);
	loop->setEnabled(selected && can_edit);
	animate_head->setEnabled(selected && can_edit);
	copy_name->setEnabled(selected && can_edit);
	save_copy->setEnabled(selected && can_edit);
	remove_button->setEnabled(selected && can_edit);
	perform_button->setEnabled(selected && can_edit);
	export_button->setEnabled(selected);
	import_button->setEnabled(can_edit && !busy);
	stop_button->setEnabled(can_edit && !busy);
	table->setEnabled(!busy);
}

void AnimationEditorPanel::updateTime()
{
	time_label->setText(tr("%1 / %2 с").arg(duration * timeline->value() / 10000.0, 0, 'f', 2).arg(duration, 0, 'f', 2));
}
