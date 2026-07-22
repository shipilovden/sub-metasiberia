/*=====================================================================
DocumentEditorPanel.cpp
-----------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "DocumentEditorPanel.h"


#include "LucideIconUtils.h"
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTextStream>
#include <QtGui/QPalette>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtWidgets/QCheckBox>
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtGui/QAction>
#else
#include <QtWidgets/QAction>
#endif
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#if defined(METASIBERIA_QT_PDF_AVAILABLE)
#include <QtPdf/QPdfDocument>
#include <QtPdfWidgets/QPdfView>
#endif


namespace
{
static const qint64 MAX_EDITABLE_DOCUMENT_BYTES = 16 * 1024 * 1024;


static QToolButton* makeToolButton(QWidget* parent, const QString& tool_tip, bool checkable = false)
{
	QToolButton* button = new QToolButton(parent);
	button->setAutoRaise(true);
	button->setCheckable(checkable);
	button->setToolTip(tool_tip);
	button->setAccessibleName(tool_tip);
	return button;
}


static std::string toStdString(const QString& value)
{
	const QByteArray bytes = value.toUtf8();
	return std::string(bytes.constData(), (std::size_t)bytes.size());
}


static QString toQString(const std::string& value)
{
	return QString::fromUtf8(value.data(), (int)value.size());
}
}


DocumentEditorPanel::DocumentEditorPanel(QWidget* parent)
:	QWidget(parent),
	current_format(FormatPlainText),
	document_modified(false),
	updating_editors(false),
	read_only(false),
	open_button(0),
	save_button(0),
	export_button(0),
	source_mode_button(0),
	previous_match_button(0),
	next_match_button(0),
	search_edit(0),
	file_label(0),
	status_label(0),
	tabs(0),
	editor_stack(0),
	source_editor(0),
	preview_editor(0),
	title_edit(0),
	author_edit(0),
	description_edit(0),
	language_edit(0),
	tags_edit(0),
	source_url_edit(0),
	resource_url_edit(0),
	linked_object_edit(0),
	select_linked_object_button(0),
	sources_edit(0),
	comments_edit(0),
	display_mode_combo(0),
	open_on_click_check(0),
	pinned_to_object_check(0),
	allow_selection_check(0),
	show_toolbar_check(0),
	initial_page_spin(0),
	zoom_spin(0),
	screen_width_spin(0),
	screen_height_spin(0),
	rotation_spin(0),
	pdf_path_edit(0),
	pdf_status_label(0),
	open_pdf_button(0),
	page_texture_button(0)
#if defined(METASIBERIA_QT_PDF_AVAILABLE)
	,pdf_document(0),
	pdf_view(0)
#endif
{
	buildUi();
	clearDocument();
}


DocumentEditorPanel::~DocumentEditorPanel()
{}


void DocumentEditorPanel::buildUi()
{
	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);

	QHBoxLayout* toolbar = new QHBoxLayout();
	open_button = makeToolButton(this, tr("Открыть TXT, HTML, Markdown или PDF"));
	save_button = makeToolButton(this, tr("Сохранить документ"));
	export_button = makeToolButton(this, tr("Экспортировать документ"));
	export_button->setPopupMode(QToolButton::InstantPopup);
	QMenu* export_menu = new QMenu(export_button);
	QAction* export_html = export_menu->addAction(tr("Экспортировать как HTML"));
	QAction* export_markdown = export_menu->addAction(tr("Экспортировать как Markdown"));
	QAction* export_text = export_menu->addAction(tr("Экспортировать как обычный текст"));
	export_button->setMenu(export_menu);
	source_mode_button = makeToolButton(this, tr("Переключить исходный текст и форматированный редактор"), true);
	source_mode_button->setChecked(true);
	file_label = new QLabel(tr("Новый текстовый документ"), this);
	file_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	file_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	toolbar->addWidget(open_button);
	toolbar->addWidget(save_button);
	toolbar->addWidget(export_button);
	toolbar->addWidget(source_mode_button);
	toolbar->addWidget(file_label, 1);
	root->addLayout(toolbar);

	QHBoxLayout* search_row = new QHBoxLayout();
	search_edit = new QLineEdit(this);
	search_edit->setClearButtonEnabled(true);
	search_edit->setPlaceholderText(tr("Поиск в документе..."));
	previous_match_button = makeToolButton(this, tr("Предыдущее совпадение"));
	next_match_button = makeToolButton(this, tr("Следующее совпадение"));
	search_row->addWidget(search_edit, 1);
	search_row->addWidget(previous_match_button);
	search_row->addWidget(next_match_button);
	root->addLayout(search_row);

	tabs = new QTabWidget(this);
	tabs->setDocumentMode(true);
	tabs->setUsesScrollButtons(true);

	QWidget* editor_page = new QWidget(tabs);
	QVBoxLayout* editor_layout = new QVBoxLayout(editor_page);
	editor_layout->setContentsMargins(0, 0, 0, 0);
	editor_stack = new QStackedWidget(editor_page);
	source_editor = new QTextEdit(editor_stack);
	source_editor->setAcceptRichText(false);
	source_editor->setTabStopWidth(32);
	source_editor->setPlaceholderText(tr("Введите текст, HTML или Markdown..."));
	preview_editor = new QTextEdit(editor_stack);
	preview_editor->setAcceptRichText(true);
	preview_editor->setPlaceholderText(tr("Форматированный документ"));
	editor_stack->addWidget(source_editor);
	editor_stack->addWidget(preview_editor);
	editor_layout->addWidget(editor_stack);
	tabs->addTab(editor_page, tr("Документ"));
	tabs->addTab(buildMetadataPage(), tr("Метаданные"));
	tabs->addTab(buildPdfPage(), tr("PDF"));
	root->addWidget(tabs, 1);

	status_label = new QLabel(this);
	status_label->setWordWrap(true);
	status_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	root->addWidget(status_label);

	connect(open_button, &QToolButton::clicked, this, &DocumentEditorPanel::openDocumentDialog);
	connect(save_button, &QToolButton::clicked, this, &DocumentEditorPanel::saveDocument);
	connect(export_html, &QAction::triggered, this, [this]() { exportDocument(FormatHtml); });
	connect(export_markdown, &QAction::triggered, this, [this]() { exportDocument(FormatMarkdown); });
	connect(export_text, &QAction::triggered, this, [this]() { exportDocument(FormatPlainText); });
	connect(source_mode_button, &QToolButton::toggled, this, &DocumentEditorPanel::showSourceMode);
	connect(previous_match_button, &QToolButton::clicked, this, [this]() { findText(true); });
	connect(next_match_button, &QToolButton::clicked, this, [this]() { findText(false); });
	connect(search_edit, &QLineEdit::returnPressed, this, [this]() { findText(false); });
	connect(source_editor, &QTextEdit::textChanged, this, [this]() {
		if(!updating_editors)
		{
			setModified(true);
			emit descriptorChanged();
		}
	});
	connect(preview_editor, &QTextEdit::textChanged, this, [this]() {
		if(!updating_editors)
		{
			setModified(true);
			emit descriptorChanged();
		}
	});

	setStyleSheet(QStringLiteral(
		"QToolButton:hover, QPushButton:hover { background: palette(highlight); color: palette(highlighted-text); }"
		"QToolButton:checked { background: palette(highlight); color: palette(highlighted-text); border: 1px solid palette(highlight); }"));
}


QWidget* DocumentEditorPanel::buildMetadataPage()
{
	QWidget* contents = new QWidget(this);
	QVBoxLayout* root = new QVBoxLayout(contents);
	root->setContentsMargins(6, 6, 6, 6);

	QGroupBox* metadata_group = new QGroupBox(tr("Описание документа"), contents);
	QFormLayout* metadata_form = new QFormLayout(metadata_group);
	title_edit = new QLineEdit(metadata_group);
	author_edit = new QLineEdit(metadata_group);
	description_edit = new QTextEdit(metadata_group);
	description_edit->setMaximumHeight(90);
	language_edit = new QLineEdit(metadata_group);
	tags_edit = new QLineEdit(metadata_group);
	source_url_edit = new QLineEdit(metadata_group);
	resource_url_edit = new QLineEdit(metadata_group);
	metadata_form->addRow(tr("Название"), title_edit);
	metadata_form->addRow(tr("Автор"), author_edit);
	metadata_form->addRow(tr("Описание"), description_edit);
	metadata_form->addRow(tr("Язык"), language_edit);
	metadata_form->addRow(tr("Теги"), tags_edit);
	metadata_form->addRow(tr("URL источника"), source_url_edit);
	metadata_form->addRow(tr("Ресурс документа"), resource_url_edit);
	root->addWidget(metadata_group);

	QGroupBox* links_group = new QGroupBox(tr("Связи и источники"), contents);
	QFormLayout* links_form = new QFormLayout(links_group);
	QWidget* linked_row = new QWidget(links_group);
	QHBoxLayout* linked_layout = new QHBoxLayout(linked_row);
	linked_layout->setContentsMargins(0, 0, 0, 0);
	linked_object_edit = new QLineEdit(linked_row);
	linked_object_edit->setPlaceholderText(tr("UID или UUID объекта"));
	select_linked_object_button = makeToolButton(linked_row, tr("Выбрать связанный объект в мире"));
	linked_layout->addWidget(linked_object_edit, 1);
	linked_layout->addWidget(select_linked_object_button);
	sources_edit = new QTextEdit(links_group);
	sources_edit->setMaximumHeight(80);
	sources_edit->setPlaceholderText(tr("Источники и библиография, по одному на строку"));
	comments_edit = new QTextEdit(links_group);
	comments_edit->setMaximumHeight(80);
	comments_edit->setPlaceholderText(tr("Комментарии к документу"));
	links_form->addRow(tr("Связанный объект"), linked_row);
	links_form->addRow(tr("Источники"), sources_edit);
	links_form->addRow(tr("Комментарии"), comments_edit);
	root->addWidget(links_group);

	QGroupBox* display_group = new QGroupBox(tr("Отображение в мире"), contents);
	QFormLayout* display_form = new QFormLayout(display_group);
	display_mode_combo = new QComboBox(display_group);
	display_mode_combo->addItem(tr("Виртуальный экран"), QStringLiteral("virtual_screen"));
	display_mode_combo->addItem(tr("Панель рядом с объектом"), QStringLiteral("attached_panel"));
	display_mode_combo->addItem(tr("Отдельная страница как текстура"), QStringLiteral("page_texture"));
	open_on_click_check = new QCheckBox(tr("Открывать по клику"), display_group);
	pinned_to_object_check = new QCheckBox(tr("Прикрепить к связанному объекту"), display_group);
	allow_selection_check = new QCheckBox(tr("Разрешить выделение и копирование текста"), display_group);
	show_toolbar_check = new QCheckBox(tr("Показывать панель навигации"), display_group);
	initial_page_spin = new QSpinBox(display_group);
	initial_page_spin->setRange(0, 1000000);
	initial_page_spin->setSpecialValueText(tr("Первая"));
	zoom_spin = new QDoubleSpinBox(display_group);
	zoom_spin->setRange(0.05, 32.0);
	zoom_spin->setDecimals(2);
	zoom_spin->setSingleStep(0.1);
	screen_width_spin = new QDoubleSpinBox(display_group);
	screen_width_spin->setRange(0.05, 100.0);
	screen_width_spin->setSuffix(tr(" м"));
	screen_height_spin = new QDoubleSpinBox(display_group);
	screen_height_spin->setRange(0.05, 100.0);
	screen_height_spin->setSuffix(tr(" м"));
	rotation_spin = new QDoubleSpinBox(display_group);
	rotation_spin->setRange(-36000.0, 36000.0);
	rotation_spin->setSuffix(tr("°"));
	display_form->addRow(tr("Режим"), display_mode_combo);
	display_form->addRow(open_on_click_check);
	display_form->addRow(pinned_to_object_check);
	display_form->addRow(allow_selection_check);
	display_form->addRow(show_toolbar_check);
	display_form->addRow(tr("Начальная страница"), initial_page_spin);
	display_form->addRow(tr("Масштаб"), zoom_spin);
	display_form->addRow(tr("Ширина экрана"), screen_width_spin);
	display_form->addRow(tr("Высота экрана"), screen_height_spin);
	display_form->addRow(tr("Поворот"), rotation_spin);
	root->addWidget(display_group);
	root->addStretch(1);

	const QList<QLineEdit*> line_edits = { title_edit, author_edit, language_edit, tags_edit, source_url_edit, resource_url_edit, linked_object_edit };
	for(QLineEdit* edit : line_edits)
		connect(edit, &QLineEdit::textChanged, this, [this]() { emit descriptorChanged(); });
	connect(description_edit, &QTextEdit::textChanged, this, [this]() { emit descriptorChanged(); });
	connect(sources_edit, &QTextEdit::textChanged, this, [this]() { emit descriptorChanged(); });
	connect(comments_edit, &QTextEdit::textChanged, this, [this]() { emit descriptorChanged(); });
	connect(select_linked_object_button, &QToolButton::clicked, this, &DocumentEditorPanel::requestLinkedObjectSelection);
	connect(display_mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { emit descriptorChanged(); });
	connect(open_on_click_check, &QCheckBox::toggled, this, [this](bool) { emit descriptorChanged(); });
	connect(pinned_to_object_check, &QCheckBox::toggled, this, [this](bool) { emit descriptorChanged(); });
	connect(allow_selection_check, &QCheckBox::toggled, this, [this](bool) { emit descriptorChanged(); });
	connect(show_toolbar_check, &QCheckBox::toggled, this, [this](bool) { emit descriptorChanged(); });
	connect(initial_page_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { emit descriptorChanged(); });
	connect(zoom_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { emit descriptorChanged(); });
	connect(screen_width_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { emit descriptorChanged(); });
	connect(screen_height_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { emit descriptorChanged(); });
	connect(rotation_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { emit descriptorChanged(); });

	QScrollArea* scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(contents);
	return scroll;
}


QWidget* DocumentEditorPanel::buildPdfPage()
{
	QWidget* page = new QWidget(this);
	QVBoxLayout* root = new QVBoxLayout(page);

	QHBoxLayout* file_row = new QHBoxLayout();
	pdf_path_edit = new QLineEdit(page);
	pdf_path_edit->setReadOnly(true);
	pdf_path_edit->setPlaceholderText(tr("PDF-файл не выбран"));
	open_pdf_button = new QPushButton(tr("Выбрать PDF"), page);
	file_row->addWidget(pdf_path_edit, 1);
	file_row->addWidget(open_pdf_button);
	root->addLayout(file_row);

	pdf_status_label = new QLabel(page);
	pdf_status_label->setWordWrap(true);
	pdf_status_label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
	root->addWidget(pdf_status_label);

#if defined(METASIBERIA_QT_PDF_AVAILABLE)
	pdf_document = new QPdfDocument(this);
	pdf_view = new QPdfView(page);
	pdf_view->setDocument(pdf_document);
	root->addWidget(pdf_view, 1);
	pdf_status_label->setText(tr("Qt PDF подключён. Выберите документ для просмотра."));
#else
	QLabel* unavailable = new QLabel(
		tr("Модуль Qt PDF (QPdfDocument/QPdfView) не установлен в текущей сборке Qt 5.15. "
		   "PDF можно сохранить как ссылку объекта, но клиент не имитирует его просмотр. "
		   "Для просмотра требуется установить QtPdf и QtPdfWidgets и собрать клиент с "
		   "METASIBERIA_QT_PDF_AVAILABLE."), page);
	unavailable->setWordWrap(true);
	unavailable->setAlignment(Qt::AlignCenter);
	unavailable->setFrameShape(QFrame::StyledPanel);
	unavailable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	root->addWidget(unavailable, 1);
	pdf_status_label->setText(tr("PDF-просмотр недоступен: Qt PDF не установлен."));
#endif

	page_texture_button = new QPushButton(tr("Создать текстуру из текущей страницы"), page);
	page_texture_button->setToolTip(tr("Будущая интеграция: отрендерить выбранную PDF-страницу в текстуру объекта"));
	page_texture_button->setEnabled(hasQtPdfSupport());
	root->addWidget(page_texture_button);

	connect(open_pdf_button, &QPushButton::clicked, this, [this]() {
		const QString path = QFileDialog::getOpenFileName(this, tr("Открыть PDF"), QString(), tr("PDF (*.pdf)"));
		if(!path.isEmpty())
			loadFile(path);
	});
	connect(page_texture_button, &QPushButton::clicked, this, [this]() {
		emit pdfPageTextureRequested(pdf_path_edit->text(), initial_page_spin ? initial_page_spin->value() : 0);
	});
	return page;
}


void DocumentEditorPanel::setIconDirectory(const QString& directory)
{
	icon_directory = directory;
	applyIcons();
}


void DocumentEditorPanel::applyIcons()
{
	if(icon_directory.isEmpty())
		return;
	const QColor colour = LucideIconUtils::themeAwareColour(
		QColor(QStringLiteral("#22D3EE")), palette(), QPalette::ButtonText, QPalette::Button);
	LucideIconUtils::setButtonIcon(open_button, icon_directory, QStringLiteral("folder-open"), colour);
	LucideIconUtils::setButtonIcon(save_button, icon_directory, QStringLiteral("save"), colour);
	LucideIconUtils::setButtonIcon(export_button, icon_directory, QStringLiteral("file-output"), colour);
	LucideIconUtils::setButtonIcon(source_mode_button, icon_directory, QStringLiteral("file-text"), colour);
	LucideIconUtils::setButtonIcon(previous_match_button, icon_directory, QStringLiteral("arrow-up"), colour);
	LucideIconUtils::setButtonIcon(next_match_button, icon_directory, QStringLiteral("arrow-down"), colour);
	LucideIconUtils::setButtonIcon(select_linked_object_button, icon_directory, QStringLiteral("panels-top-left"), colour);
	LucideIconUtils::setButtonIcon(open_pdf_button, icon_directory, QStringLiteral("folder-open"), colour);
	LucideIconUtils::setButtonIcon(page_texture_button, icon_directory, QStringLiteral("file-output"), colour);
}


void DocumentEditorPanel::setReadOnly(bool new_read_only)
{
	read_only = new_read_only;
	source_editor->setReadOnly(read_only);
	preview_editor->setReadOnly(read_only);
	const QList<QLineEdit*> line_edits = { title_edit, author_edit, language_edit, tags_edit, source_url_edit, resource_url_edit, linked_object_edit };
	for(QLineEdit* edit : line_edits)
		edit->setReadOnly(read_only);
	description_edit->setReadOnly(read_only);
	sources_edit->setReadOnly(read_only);
	comments_edit->setReadOnly(read_only);
	display_mode_combo->setEnabled(!read_only);
	open_on_click_check->setEnabled(!read_only);
	pinned_to_object_check->setEnabled(!read_only);
	allow_selection_check->setEnabled(!read_only);
	show_toolbar_check->setEnabled(!read_only);
	initial_page_spin->setEnabled(!read_only);
	zoom_spin->setEnabled(!read_only);
	screen_width_spin->setEnabled(!read_only);
	screen_height_spin->setEnabled(!read_only);
	rotation_spin->setEnabled(!read_only);
	select_linked_object_button->setEnabled(!read_only);
	updateUiState();
}


void DocumentEditorPanel::clearDocument()
{
	setDocumentSettings(DocumentObjectSettings::defaultObject());
	updating_editors = true;
	source_editor->clear();
	preview_editor->clear();
	updating_editors = false;
	current_file_path.clear();
	current_format = FormatPlainText;
	pdf_path_edit->clear();
	setModified(false);
	showSourceMode(true);
	showStatus(tr("Новый текстовый документ."));
	updateUiState();
}


bool DocumentEditorPanel::loadFile(const QString& path)
{
	if(path.isEmpty())
		return false;
	if(!maybeDiscardChanges())
		return false;

	const QFileInfo info(path);
	if(!info.exists() || !info.isFile())
	{
		showStatus(tr("Файл не найден: %1").arg(path), true);
		return false;
	}

	const DocumentFormat format = formatFromPath(path);
	if(format == FormatUnknown)
	{
		showStatus(tr("Неподдерживаемый формат. Откройте TXT, HTML, Markdown или PDF."), true);
		return false;
	}

	if(format == FormatPdf)
	{
		setCurrentDocument(path, format);
		setPdfPath(path);
		tabs->setCurrentIndex(2);
		if(title_edit->text().trimmed().isEmpty())
			title_edit->setText(info.completeBaseName());
		setModified(false);
		emit documentOpened(path, formatId(format));
		return true;
	}

	if(info.size() > MAX_EDITABLE_DOCUMENT_BYTES)
	{
		showStatus(tr("Документ больше 16 МиБ и не открыт во встроенном редакторе."), true);
		return false;
	}

	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
	{
		showStatus(tr("Не удалось открыть файл: %1").arg(file.errorString()), true);
		return false;
	}
	QByteArray bytes = file.readAll();
	if(bytes.startsWith("\xEF\xBB\xBF"))
		bytes.remove(0, 3);

	updating_editors = true;
	source_editor->setPlainText(QString::fromUtf8(bytes));
	updating_editors = false;
	setCurrentDocument(path, format);
	renderPreviewFromSource();
	showSourceMode(true);
	tabs->setCurrentIndex(0);
	if(title_edit->text().trimmed().isEmpty())
		title_edit->setText(info.completeBaseName());
	setModified(false);
	showStatus(tr("Открыт %1: %2").arg(formatName(format), path));
	emit documentOpened(path, formatId(format));
	return true;
}


bool DocumentEditorPanel::saveDocument()
{
	if(current_format == FormatPdf)
	{
		showStatus(tr("PDF открыт только для просмотра/ссылки и не редактируется этим модулем."), true);
		return false;
	}

	QString path = current_file_path;
	if(path.isEmpty())
	{
		path = QFileDialog::getSaveFileName(this, tr("Сохранить документ"), title_edit->text(), fileDialogFilter(current_format));
		if(path.isEmpty())
			return false;
	}
	return saveTextToPath(path, current_format, true);
}


bool DocumentEditorPanel::exportDocument(DocumentFormat format, const QString& requested_path)
{
	if(format != FormatPlainText && format != FormatHtml && format != FormatMarkdown)
	{
		showStatus(tr("Этот формат экспорта пока не поддерживается."), true);
		return false;
	}

	QString path = requested_path;
	if(path.isEmpty())
		path = QFileDialog::getSaveFileName(this, tr("Экспортировать документ"), title_edit->text(), fileDialogFilter(format));
	if(path.isEmpty())
		return false;
	return saveTextToPath(path, format, false);
}


bool DocumentEditorPanel::saveTextToPath(const QString& path, DocumentFormat format, bool adopt_as_current_document)
{
	if(path.isEmpty())
		return false;
	const QString text = textForFormat(format);
	QSaveFile file(path);
	if(!file.open(QIODevice::WriteOnly))
	{
		showStatus(tr("Не удалось сохранить файл: %1").arg(file.errorString()), true);
		return false;
	}
	const QByteArray bytes = text.toUtf8();
	if(file.write(bytes) != bytes.size() || !file.commit())
	{
		showStatus(tr("Не удалось завершить сохранение файла: %1").arg(file.errorString()), true);
		return false;
	}

	if(adopt_as_current_document)
	{
		setCurrentDocument(path, format);
		setModified(false);
	}
	showStatus(adopt_as_current_document ? tr("Документ сохранён: %1").arg(path) : tr("Документ экспортирован: %1").arg(path));
	emit documentSaved(path, formatId(format));
	return true;
}


QString DocumentEditorPanel::textForFormat(DocumentFormat format)
{
	if(format == current_format && source_mode_button->isChecked())
		return source_editor->toPlainText();
	if(source_mode_button->isChecked())
		renderPreviewFromSource();

	switch(format)
	{
	case FormatHtml: return preview_editor->document()->toHtml();
	case FormatMarkdown: return preview_editor->document()->toMarkdown();
	case FormatPlainText: return preview_editor->toPlainText();
	default: return QString();
	}
}


void DocumentEditorPanel::openDocumentDialog()
{
	const QString path = QFileDialog::getOpenFileName(this, tr("Открыть документ"), QString(),
		tr("Документы (*.txt *.text *.html *.htm *.md *.markdown *.pdf);;Текст (*.txt *.text);;HTML (*.html *.htm);;Markdown (*.md *.markdown);;PDF (*.pdf)"));
	if(!path.isEmpty())
		loadFile(path);
}


void DocumentEditorPanel::showSourceMode(bool source_mode)
{
	if(source_mode)
	{
		if(editor_stack->currentWidget() == preview_editor)
			syncSourceFromPreview();
		editor_stack->setCurrentWidget(source_editor);
		source_mode_button->setToolTip(tr("Показан исходный текст. Нажмите для форматированного редактора."));
	}
	else
	{
		renderPreviewFromSource();
		editor_stack->setCurrentWidget(preview_editor);
		source_mode_button->setToolTip(tr("Показан форматированный редактор. Нажмите для исходного текста."));
	}
}


void DocumentEditorPanel::renderPreviewFromSource()
{
	updating_editors = true;
	const QString source = source_editor->toPlainText();
	switch(current_format)
	{
	case FormatHtml: preview_editor->document()->setHtml(source); break;
	case FormatMarkdown: preview_editor->document()->setMarkdown(source); break;
	default: preview_editor->document()->setPlainText(source); break;
	}
	updating_editors = false;
}


void DocumentEditorPanel::syncSourceFromPreview()
{
	updating_editors = true;
	switch(current_format)
	{
	case FormatHtml: source_editor->setPlainText(preview_editor->document()->toHtml()); break;
	case FormatMarkdown: source_editor->setPlainText(preview_editor->document()->toMarkdown()); break;
	default: source_editor->setPlainText(preview_editor->toPlainText()); break;
	}
	updating_editors = false;
}


void DocumentEditorPanel::findText(bool backwards)
{
	const QString query = search_edit->text();
	if(query.isEmpty())
		return;
	QTextEdit* editor = editor_stack->currentWidget() == source_editor ? source_editor : preview_editor;
	QTextDocument::FindFlags flags;
	if(backwards)
		flags |= QTextDocument::FindBackward;
	QTextCursor found = editor->document()->find(query, editor->textCursor(), flags);
	if(found.isNull())
	{
		QTextCursor wrap_cursor(editor->document());
		wrap_cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
		found = editor->document()->find(query, wrap_cursor, flags);
	}
	if(!found.isNull())
	{
		editor->setTextCursor(found);
		editor->ensureCursorVisible();
		showStatus(tr("Найдено: %1").arg(query));
	}
	else
		showStatus(tr("Текст не найден: %1").arg(query), true);
}


void DocumentEditorPanel::requestLinkedObjectSelection()
{
	emit linkedObjectSelectionRequested(linked_object_edit->text().trimmed());
}


bool DocumentEditorPanel::maybeDiscardChanges()
{
	if(!document_modified)
		return true;
	const QMessageBox::StandardButton answer = QMessageBox::question(this, tr("Несохранённые изменения"),
		tr("В документе есть несохранённые изменения. Открыть другой файл и потерять их?"),
		QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
	return answer == QMessageBox::Discard;
}


void DocumentEditorPanel::setCurrentDocument(const QString& path, DocumentFormat format)
{
	current_file_path = path;
	current_format = format;
	updateUiState();
	emit descriptorChanged();
}


void DocumentEditorPanel::setPdfPath(const QString& path)
{
	pdf_path_edit->setText(path);
#if defined(METASIBERIA_QT_PDF_AVAILABLE)
	pdf_document->close();
	pdf_document->load(path);
	pdf_status_label->setText(tr("PDF загружен через Qt PDF: %1").arg(path));
	page_texture_button->setEnabled(true);
#else
	pdf_status_label->setText(tr("PDF сохранён как ссылка, но просмотр недоступен без Qt PDF: %1").arg(path));
	page_texture_button->setEnabled(false);
#endif
	showStatus(pdf_status_label->text());
}


void DocumentEditorPanel::setModified(bool modified)
{
	if(document_modified == modified)
		return;
	document_modified = modified;
	emit documentModifiedChanged(document_modified);
	updateUiState();
}


void DocumentEditorPanel::showStatus(const QString& message, bool error)
{
	status_label->setText(message);
	QPalette status_palette = status_label->palette();
	status_palette.setColor(QPalette::WindowText, error ? QColor(QStringLiteral("#FB7185")) : palette().color(QPalette::Text));
	status_label->setPalette(status_palette);
	emit statusMessageChanged(message);
	if(error)
		emit errorOccurred(message);
}


void DocumentEditorPanel::updateUiState()
{
	const bool editable_text = !read_only && current_format != FormatPdf;
	save_button->setEnabled(editable_text);
	export_button->setEnabled(current_format != FormatPdf);
	source_mode_button->setEnabled(current_format != FormatPdf);
	file_label->setText((current_file_path.isEmpty() ? tr("Новый %1").arg(formatName(current_format)) : QFileInfo(current_file_path).fileName()) +
		(document_modified ? QStringLiteral(" *") : QString()));
}


DocumentObjectSettings DocumentEditorPanel::documentSettings() const
{
	DocumentObjectSettings settings = current_settings;
	settings.title = toStdString(title_edit->text().trimmed());
	settings.author = toStdString(author_edit->text().trimmed());
	settings.description = toStdString(description_edit->toPlainText());
	settings.language = toStdString(language_edit->text().trimmed());
	settings.tags = toStdString(tags_edit->text());
	settings.document_format = toStdString(formatId(current_format));
	settings.source_mode = !current_file_path.isEmpty() ? "local_file" : (!source_url_edit->text().trimmed().isEmpty() ? "url" : "manual");
	settings.local_file_path = toStdString(current_file_path);
	settings.source_url = toStdString(source_url_edit->text().trimmed());
	settings.resource_url = toStdString(resource_url_edit->text().trimmed());
	bool linked_id_is_uid = false;
	linked_object_edit->text().trimmed().toULongLong(&linked_id_is_uid);
	if(linked_id_is_uid)
	{
		settings.linked_object_uid = toStdString(linked_object_edit->text().trimmed());
		settings.linked_object_uuid.clear();
	}
	else
	{
		settings.linked_object_uuid = toStdString(linked_object_edit->text().trimmed());
		settings.linked_object_uid.clear();
	}
	settings.sources = toStdString(sources_edit->toPlainText());
	settings.comments = toStdString(comments_edit->toPlainText());
	settings.display_mode = toStdString(display_mode_combo->currentData().toString());
	settings.open_on_click = open_on_click_check->isChecked();
	settings.pinned_to_object = pinned_to_object_check->isChecked();
	settings.allow_text_selection = allow_selection_check->isChecked();
	settings.show_toolbar = show_toolbar_check->isChecked();
	settings.initial_page = initial_page_spin->value();
	settings.zoom = (float)zoom_spin->value();
	settings.screen_width = (float)screen_width_spin->value();
	settings.screen_height = (float)screen_height_spin->value();
	settings.rotation_degrees = (float)rotation_spin->value();
	return settings;
}


void DocumentEditorPanel::setDocumentSettings(const DocumentObjectSettings& settings)
{
	current_settings = settings;
	const QList<QObject*> blockers = { title_edit, author_edit, description_edit, language_edit, tags_edit, source_url_edit,
		resource_url_edit, linked_object_edit, sources_edit, comments_edit, display_mode_combo, open_on_click_check,
		pinned_to_object_check, allow_selection_check, show_toolbar_check, initial_page_spin, zoom_spin,
		screen_width_spin, screen_height_spin, rotation_spin };
	QList<QSignalBlocker*> signal_blockers;
	for(QObject* object : blockers)
		signal_blockers.push_back(new QSignalBlocker(object));

	title_edit->setText(toQString(settings.title));
	author_edit->setText(toQString(settings.author));
	description_edit->setPlainText(toQString(settings.description));
	language_edit->setText(toQString(settings.language));
	tags_edit->setText(toQString(settings.tags));
	source_url_edit->setText(toQString(settings.source_url));
	resource_url_edit->setText(toQString(settings.resource_url));
	linked_object_edit->setText(!settings.linked_object_uuid.empty() ? toQString(settings.linked_object_uuid) : toQString(settings.linked_object_uid));
	sources_edit->setPlainText(toQString(settings.sources));
	comments_edit->setPlainText(toQString(settings.comments));
	const int display_index = display_mode_combo->findData(toQString(settings.display_mode));
	if(display_index >= 0)
		display_mode_combo->setCurrentIndex(display_index);
	open_on_click_check->setChecked(settings.open_on_click);
	pinned_to_object_check->setChecked(settings.pinned_to_object);
	allow_selection_check->setChecked(settings.allow_text_selection);
	show_toolbar_check->setChecked(settings.show_toolbar);
	initial_page_spin->setValue(settings.initial_page);
	zoom_spin->setValue(settings.zoom);
	screen_width_spin->setValue(settings.screen_width);
	screen_height_spin->setValue(settings.screen_height);
	rotation_spin->setValue(settings.rotation_degrees);

	for(QSignalBlocker* blocker : signal_blockers)
		delete blocker;

	current_format = formatFromPath(toQString(settings.local_file_path));
	if(current_format == FormatUnknown)
	{
		const QString id = toQString(settings.document_format);
		if(id == QStringLiteral("html")) current_format = FormatHtml;
		else if(id == QStringLiteral("markdown")) current_format = FormatMarkdown;
		else if(id == QStringLiteral("pdf")) current_format = FormatPdf;
		else current_format = FormatPlainText;
	}
	current_file_path = toQString(settings.local_file_path);
	if(current_format == FormatPdf)
		pdf_path_edit->setText(current_file_path);
	setModified(false);
	updateUiState();
}


bool DocumentEditorPanel::hasQtPdfSupport()
{
#if defined(METASIBERIA_QT_PDF_AVAILABLE)
	return true;
#else
	return false;
#endif
}


DocumentEditorPanel::DocumentFormat DocumentEditorPanel::formatFromPath(const QString& path)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	if(suffix == QStringLiteral("txt") || suffix == QStringLiteral("text")) return FormatPlainText;
	if(suffix == QStringLiteral("html") || suffix == QStringLiteral("htm")) return FormatHtml;
	if(suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")) return FormatMarkdown;
	if(suffix == QStringLiteral("pdf")) return FormatPdf;
	return FormatUnknown;
}


QString DocumentEditorPanel::formatId(DocumentFormat format)
{
	switch(format)
	{
	case FormatPlainText: return QStringLiteral("plain_text");
	case FormatHtml: return QStringLiteral("html");
	case FormatMarkdown: return QStringLiteral("markdown");
	case FormatPdf: return QStringLiteral("pdf");
	default: return QStringLiteral("unknown");
	}
}


QString DocumentEditorPanel::formatName(DocumentFormat format)
{
	switch(format)
	{
	case FormatPlainText: return tr("текстовый документ");
	case FormatHtml: return QStringLiteral("HTML");
	case FormatMarkdown: return QStringLiteral("Markdown");
	case FormatPdf: return QStringLiteral("PDF");
	default: return tr("документ");
	}
}


QString DocumentEditorPanel::fileDialogFilter(DocumentFormat format)
{
	switch(format)
	{
	case FormatHtml: return tr("HTML (*.html *.htm)");
	case FormatMarkdown: return tr("Markdown (*.md *.markdown)");
	case FormatPlainText: return tr("Текст (*.txt)");
	case FormatPdf: return tr("PDF (*.pdf)");
	default: return tr("Все файлы (*)");
	}
}
