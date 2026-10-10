/*=====================================================================
AIDockWidget.cpp
----------------
=====================================================================*/
#include "AIDockWidget.h"
#include "LucideIconUtils.h"
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QSpinBox>
#include <QtCore/QSignalBlocker>


#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QDialog>
#include <QtWidgets/QScrollBar>
#include <QtGui/QPainter>
#include <QtGui/QTextImageFormat>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>
#include <QtCore/QTimer>
#include <QtGui/QTextCursor>
#include <QtGui/QTextBlock>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QListWidget>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeData>
#include <QtCore/QUuid>
#include <QtGui/QClipboard>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QImageReader>
#include <QtGui/QKeyEvent>


namespace {
// The world media viewer uses GL world objects. References remain private local
// attachments, so this Qt viewer reproduces its fit/black background/Esc behaviour.
class AIReferenceViewer final : public QDialog
{
public:
    AIReferenceViewer(const QString& path, QWidget* parent) : QDialog(parent), image(path)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setWindowModality(Qt::WindowModal);
        QVBoxLayout* layout = new QVBoxLayout(this);
        QHBoxLayout* bar = new QHBoxLayout();
        QLabel* hint = new QLabel(AIDockWidget::tr("Reference — Esc to close"), this);
        hint->setStyleSheet("color: white; background: transparent;");
        QPushButton* close = new QPushButton(QString::fromUtf8("×"), this);
        close->setToolTip(AIDockWidget::tr("Close"));
        close->setAccessibleName(AIDockWidget::tr("Close"));
        close->setFixedSize(36, 36);
        bar->addWidget(hint); bar->addStretch(); bar->addWidget(close);
        layout->addLayout(bar); layout->addStretch();
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this); painter.fillRect(rect(), Qt::black);
        if(image.isNull()) return;
        const QRect area=rect().adjusted(12,60,-12,-12);
        const QSize size=image.size().scaled(area.size(),Qt::KeepAspectRatio);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(QRect(area.center()-QPoint(size.width()/2,size.height()/2),size),image);
    }
private:
    QPixmap image;
};
}

AIDockWidget::AIDockWidget(QWidget* parent)
	: QDockWidget(tr("AI"), parent),
	connection_label(new QLabel(this)),
	help_box(nullptr),
	help_label(nullptr),
	conversation_view(new QTextBrowser(this)),
	input_edit(new QLineEdit(this)),
	send_button(new QPushButton(tr("Send"), this)),
	attach_button(new QPushButton(this)),
	remove_reference_button(new QPushButton(this)),
	model_label(new QLabel(this)), effort_label(new QLabel(this)),
	model_combo(new QComboBox(this)), effort_combo(new QComboBox(this)),
	references(new QListWidget(this))
{
	setObjectName(QStringLiteral("aiDockWidget"));
	setAcceptDrops(true);
	setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	setMinimumWidth(260);
	setMaximumWidth(QWIDGETSIZE_MAX);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

	QWidget* content = new QWidget(this);
	content->setMinimumWidth(0);
	QVBoxLayout* outer_layout = new QVBoxLayout(content);
	outer_layout->setContentsMargins(0, 0, 0, 0);
	tabs = new QTabWidget(content);
	outer_layout->addWidget(tabs);
	QWidget* chat_page = new QWidget(tabs);
	QWidget* settings_page = new QWidget(tabs);
	tabs->addTab(chat_page, tr("Chat"));
	tabs->addTab(settings_page, tr("Settings"));
	QVBoxLayout* layout = new QVBoxLayout(chat_page);
	layout->setContentsMargins(8, 8, 8, 8);
	QVBoxLayout* settings_layout = new QVBoxLayout(settings_page);
	enable_mcp = new QCheckBox(settings_page);
	enable_mcp->setChecked(false);
	settings_layout->addWidget(enable_mcp);
	mcp_description = new QLabel(settings_page);
	mcp_description->setWordWrap(true);
	mcp_description->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	settings_layout->addWidget(mcp_description);
	QHBoxLayout* port_layout = new QHBoxLayout();
	port_label = new QLabel(settings_page);
	mcp_port = new QSpinBox(settings_page);
	mcp_port->setRange(1024, 65535);
	mcp_port->setValue(8095);
	mcp_port->setEnabled(false);
	port_layout->addWidget(port_label);
	port_layout->addWidget(mcp_port, 1);
	settings_layout->addLayout(port_layout);
	enable_poly_haven = new QCheckBox(settings_page);
	settings_layout->addWidget(enable_poly_haven);
	texture_description = new QLabel(settings_page);
	texture_description->setWordWrap(true);
	texture_description->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	settings_layout->addWidget(texture_description);
	connect(enable_poly_haven,&QCheckBox::toggled,this,&AIDockWidget::polyHavenChanged);
	image_generation_description = new QLabel(settings_page);
	image_generation_description->setWordWrap(true);
	image_generation_description->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
	settings_layout->addWidget(image_generation_description);

	connection_label->setWordWrap(true);
	connection_label->setMinimumWidth(0);
	connection_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	connection_label->setText(tr("Enable MCP in the AI Settings tab and connect to a world."));
	layout->addWidget(connection_label);

	help_box = new QGroupBox(tr("How it works"), content);
	help_label = new QLabel(tr("Enter commands here. Codex uses MCP tools for the current Metasiberia world. Codex login comes from the installed Codex CLI or VS Code extension; the game client does not store an OpenAI API key."), help_box);
	help_label->setWordWrap(true);
	help_label->setMinimumWidth(0);
	help_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	QVBoxLayout* help_layout = new QVBoxLayout(help_box);
	help_layout->addWidget(help_label);
	settings_layout->addWidget(help_box);
	settings_layout->addStretch(1);
	QHBoxLayout* model_layout = new QHBoxLayout();
	model_combo->addItem(tr("Codex default"), QJsonObject());
	model_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	model_combo->setMinimumContentsLength(8);
	model_combo->setMinimumWidth(0);
	model_combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	effort_combo->setMinimumWidth(0);
	effort_combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	model_layout->addWidget(model_label);
	model_layout->addWidget(model_combo, 2);
	model_layout->addWidget(effort_label);
	model_layout->addWidget(effort_combo, 1);
	layout->addLayout(model_layout);

	conversation_view->setReadOnly(true);
    conversation_view->setOpenLinks(false);
    conversation_view->setOpenExternalLinks(false);
    connect(conversation_view, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        const auto it=sent_references.constFind(url.toString());
        if(it!=sent_references.constEnd()) openReference(it.value());
    });
	conversation_view->viewport()->installEventFilter(this);
	conversation_view->viewport()->setAcceptDrops(true);
	conversation_view->setMinimumWidth(0);
	conversation_view->setPlaceholderText(tr("The Codex conversation will appear here…"));
	layout->addWidget(conversation_view, 1);
    thinking_row = new QWidget(chat_page);
    QHBoxLayout* thinking_layout = new QHBoxLayout(thinking_row);
    thinking_layout->setContentsMargins(0,0,0,0);
    thinking_icon = new QLabel(thinking_row); thinking_icon->setFixedSize(28,28);
    thinking_label = new QLabel(thinking_row);
    thinking_label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    thinking_layout->addWidget(thinking_icon); thinking_layout->addWidget(thinking_label,1);
    layout->addWidget(thinking_row);
    thinking_row->hide();
    thinking_timer = new QTimer(this); thinking_timer->setInterval(80);
    connect(thinking_timer,&QTimer::timeout,this,&AIDockWidget::updateThinkingIndicator);
    connect(&codex,&CodexAppServerClient::turnActiveChanged,this,&AIDockWidget::setTurnActive);
    connect(&codex,&CodexAppServerClient::activityChanged,this,[this](const QString& phase) {
        activity_phase=phase; updateThinkingIndicator();
    });
	references->setViewMode(QListView::IconMode);
	references->viewport()->installEventFilter(this);
	references->viewport()->setAcceptDrops(true);
	connect(references,&QListWidget::itemClicked,this,[this](QListWidgetItem* item) {
        openReference(item->data(Qt::UserRole).toString());
    });
    references->setIconSize(QSize(64, 64));
	references->setGridSize(QSize(86, 86));
	references->setFlow(QListView::LeftToRight);
	references->setWrapping(false);
	references->setFixedHeight(106);
	references->setMinimumWidth(0);
	references->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	references->hide();
	layout->addWidget(references);
	QHBoxLayout* input_layout = new QHBoxLayout();
	for(QPushButton* button : {attach_button, remove_reference_button})
	{
		button->setFixedSize(28, 28);
		button->setIconSize(QSize(18, 18));
		button->setFlat(true);
	}
	remove_reference_button->hide();
	input_layout->addWidget(attach_button);
	input_layout->addWidget(remove_reference_button);
	input_edit->setPlaceholderText(tr("Enter a command for Codex…"));
	input_edit->setMinimumWidth(0);
	input_edit->installEventFilter(this);
	input_layout->addWidget(input_edit, 1);
	input_layout->addWidget(send_button);
	layout->addLayout(input_layout);

	setWidget(content);

	connect(send_button, &QPushButton::clicked, this, &AIDockWidget::sendCurrentMessage);
	connect(input_edit, &QLineEdit::returnPressed, this, &AIDockWidget::sendCurrentMessage);
	connect(&codex, &CodexAppServerClient::assistantDelta, this, &AIDockWidget::appendAssistantDelta);
	connect(&codex, &CodexAppServerClient::assistantMessageStarted, this, [this]() { assistant_buffer.clear(); });
	connect(&codex, &CodexAppServerClient::assistantMessageFinished, this, &AIDockWidget::finishAssistantMessage);
	connect(&codex, &CodexAppServerClient::generatedImage, this, [this](const QString& path) {
		if(QFileInfo::exists(path)) appendReferences(QStringList{path});
	});
	connect(&codex, &CodexAppServerClient::statusChanged, this, &AIDockWidget::updateStatus);
	connect(&codex, &CodexAppServerClient::error, this, &AIDockWidget::showError);
	connect(&codex, &CodexAppServerClient::modelsAvailable, this, &AIDockWidget::updateModels);
	connect(&codex, &CodexAppServerClient::modelListFailed, this, [this](const QString& error) {
		model_combo->setToolTip(tr("Model list unavailable: %1").arg(error));
	});
	connect(model_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { updateEfforts(); });
	connect(attach_button, &QPushButton::clicked, this, [this]() {
		const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Attach references"), QString(), tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
		for(const QString& path : paths) addImageFile(path);
	});
	connect(remove_reference_button, &QPushButton::clicked, this, [this]() {
		delete references->takeItem(references->currentRow());
		references->setVisible(references->count() != 0);
		remove_reference_button->setVisible(references->count() != 0);
	});
	connect(enable_mcp, &QCheckBox::toggled, this, [this](bool enabled) {
		mcp_port->setEnabled(enabled);
		emit mcpSettingsChanged(enabled, mcp_port->value());
	});
	connect(mcp_port, &QSpinBox::editingFinished, this, [this]() {
		emit mcpSettingsChanged(enable_mcp->isChecked(), mcp_port->value());
	});
	updateEfforts();
	retranslateUi();
}


void AIDockWidget::retranslateUi()
{
	setWindowTitle(tr("AI"));
	send_button->setText(tr("Send"));
	help_box->setTitle(tr("How it works"));
	help_label->setText(tr("Enter commands here. Codex uses MCP tools for the current Metasiberia world. Codex login comes from the installed Codex CLI or VS Code extension; the game client does not store an OpenAI API key."));
	conversation_view->setPlaceholderText(tr("The Codex conversation will appear here…"));
	input_edit->setPlaceholderText(tr("Enter a command for Codex…"));
	tabs->setTabText(0, tr("Chat"));
	tabs->setTabText(1, QString());
	tabs->setTabToolTip(1, tr("Settings"));
	tabs->tabBar()->setAccessibleTabName(1, tr("Settings"));
	enable_mcp->setText(tr("Enable MCP server"));
	mcp_description->setText(tr("Allows AI to work in your current world with your account permissions. Only local applications can connect. Changes apply immediately."));
	port_label->setText(tr("Local port"));
	enable_poly_haven->setText(tr("Enable Poly Haven textures"));
	image_generation_description->setText(tr("Generate images using your ChatGPT/Codex sign-in. Ask in chat to create an image, then apply it to a world plane or one object face. No separate API key is used."));
	texture_description->setText(tr("CC0 PBR materials from Poly Haven. Downloads up to 2K, imported at up to 1024 px. No API key required. Local procedural textures are also available through MCP."));
	attach_button->setText(QString());
	attach_button->setAccessibleName(tr("Attach references"));
	attach_button->setToolTip(tr("Attach images, paste with Ctrl+V or drop them here. Up to 8 references."));
	remove_reference_button->setText(QString());
	remove_reference_button->setToolTip(tr("Remove reference"));
	remove_reference_button->setAccessibleName(tr("Remove reference"));
	model_label->setToolTip(tr("Model"));
	effort_label->setToolTip(tr("Reasoning"));
	model_combo->setAccessibleName(tr("Model"));
	effort_combo->setAccessibleName(tr("Reasoning"));
    const QString selected_effort=effort_combo->currentData().toString();
    updateEfforts();
    const int effort_index=effort_combo->findData(selected_effort);
    if(effort_index>=0) effort_combo->setCurrentIndex(effort_index);
    updateThinkingIndicator();
    if(codex.isRunning()) codex.refreshStatus();
    else connection_label->setText(tr("Enable MCP in the AI Settings tab and connect to a world."));
	updateIcons();
	if(model_combo->count() > 0 && model_combo->itemData(0).toJsonObject().isEmpty()) model_combo->setItemText(0, tr("Codex default"));
}


void AIDockWidget::openForEndpoint(const QString& endpoint, bool mcp_enabled, bool mcp_running, bool world_connected, const QString& working_directory)
{
	show();
	raise();
	// Dock state is restored after construction and can contain an old, very
	// wide AI panel.  Resize after the dock is laid out, while keeping it a
	// normal resizable QMainWindow dock.
	if(!initial_layout_done) QTimer::singleShot(0, this, [this]() {
		if(QMainWindow* main_window = qobject_cast<QMainWindow*>(parentWidget()))
		{
			// Clamp the first layout pass so an old saved state cannot consume the
			// entire viewport, then release the constraint for normal splitter use.
			setMaximumWidth(440);
			main_window->resizeDocks(QList<QDockWidget*>() << this, QList<int>() << 440, Qt::Horizontal);
			QTimer::singleShot(50, this, [this, main_window]() {
				setMaximumWidth(QWIDGETSIZE_MAX);
				main_window->resizeDocks(QList<QDockWidget*>() << this, QList<int>() << 440, Qt::Horizontal);
			});
		}
	});
	initial_layout_done = true;
	if(!mcp_enabled)
	{
		codex.stop();
		connection_label->setText(tr("Enable MCP in the AI Settings tab and connect to a world."));
		send_button->setEnabled(false);
		return;
	}
	if(!world_connected)
	{
		codex.stop();
		connection_label->setText(tr("MCP is enabled, but the client is not connected to a world."));
		send_button->setEnabled(false);
		return;
	}
	if(!mcp_running)
	{
		codex.stop();
		connection_label->setText(tr("The local MCP endpoint is not running. Check the world connection and saved login."));
		send_button->setEnabled(false);
		return;
	}

	if(codex.isRunning()) return;
	connection_label->setText(tr("MCP: %1\nStarting local Codex…").arg(endpoint));
	send_button->setEnabled(false);
	codex.start(endpoint, working_directory);
}


void AIDockWidget::sendCurrentMessage()
{
	const QString message = input_edit->text().trimmed();
	if((message.isEmpty() && references->count() == 0) || !codex.isReady())
		return;
	const QJsonObject model = model_combo->currentData().toJsonObject();
	if(references->count() && model.contains("inputModalities") && !model.value("inputModalities").toArray().contains("image"))
	{
		QMessageBox::information(this, tr("References"), tr("Select a model that supports images."));
		return;
	}
	QStringList paths;
	for(int i=0; i<references->count(); ++i) paths.append(references->item(i)->data(Qt::UserRole).toString());
	if(!codex.sendMessage(message, paths, model.value("model").toString(), effort_combo->currentData().toString())) return;

	appendLine(tr("You"), message);
	appendReferences(paths);
	input_edit->clear();
	references->clear();
	references->hide();
	remove_reference_button->hide();
	send_button->setEnabled(false);
	model_combo->setEnabled(false);
	effort_combo->setEnabled(false);
	assistant_buffer.clear();
}


void AIDockWidget::appendLine(const QString& speaker, const QString& text)
{
	QTextCursor cursor(conversation_view->document()); cursor.movePosition(QTextCursor::End);
    if(!conversation_view->document()->isEmpty()) cursor.insertBlock();
    cursor.setCharFormat(QTextCharFormat());
    cursor.insertText(speaker + QStringLiteral(": ") + text);
    conversation_view->setTextCursor(cursor);
	conversation_view->ensureCursorVisible();
}


void AIDockWidget::appendAssistantDelta(const QString& delta)
{
    const bool at_bottom=conversation_view->verticalScrollBar()->value() >= conversation_view->verticalScrollBar()->maximum()-8;
    QTextCursor cursor(conversation_view->document()); cursor.movePosition(QTextCursor::End);
    if(assistant_buffer.isEmpty()) {
        if(!conversation_view->document()->isEmpty()) cursor.insertBlock();
        cursor.setCharFormat(QTextCharFormat());
        cursor.insertText(tr("Codex") + QStringLiteral(": "));
    }
    assistant_buffer += delta;
    cursor.insertText(delta);
    if(at_bottom) { conversation_view->setTextCursor(cursor); conversation_view->ensureCursorVisible(); }
}


void AIDockWidget::finishAssistantMessage()
{
	if(!assistant_buffer.isEmpty())
	{
		QTextCursor cursor = conversation_view->textCursor();
		cursor.movePosition(QTextCursor::End);
		cursor.insertText(QStringLiteral("\n"));
		conversation_view->setTextCursor(cursor);
	}
	assistant_buffer.clear();
	send_button->setEnabled(codex.isReady());
	model_combo->setEnabled(true);
	effort_combo->setEnabled(true);
}


void AIDockWidget::updateStatus(const QString& status)
{
	connection_label->setText(status);
	send_button->setEnabled(codex.isReady());
}


void AIDockWidget::showError(const QString& message)
{
	connection_label->setText(tr("Codex error: %1").arg(message));
	send_button->setEnabled(codex.isReady());
	model_combo->setEnabled(!codex.isTurnActive());
	effort_combo->setEnabled(!codex.isTurnActive());
	assistant_buffer.clear();
	if(!message.isEmpty())
		appendLine(tr("System"), message);
}


void AIDockWidget::updateModels(const QJsonArray& models)
{
	const QString selected = model_combo->currentData().toJsonObject().value("model").toString();
	model_combo->blockSignals(true);
	model_combo->clear();
	for(const QJsonValue& value : models)
	{
		const QJsonObject model = value.toObject();
		if(model.value("model").toString().isEmpty()) continue;
		model_combo->addItem(model.value("displayName").toString(model.value("model").toString()), model);
		if(model.value("model").toString() == selected || (selected.isEmpty() && model.value("isDefault").toBool()))
			model_combo->setCurrentIndex(model_combo->count()-1);
	}
	if(model_combo->count() == 0) model_combo->addItem(tr("Codex default"), QJsonObject());
	model_combo->blockSignals(false);
	updateEfforts();
}


void AIDockWidget::updateEfforts()
{
	effort_combo->clear();
	const QJsonObject model = model_combo->currentData().toJsonObject();
	for(const QJsonValue& value : model.value("supportedReasoningEfforts").toArray())
	{
		const QJsonObject effort = value.toObject();
		const QString name = effort.value("reasoningEffort").toString();
		QString label = name;
		if(name == "low") label = tr("Low");
		else if(name == "medium") label = tr("Medium");
		else if(name == "high") label = tr("High");
		else if(name == "xhigh") label = tr("Extra high");
        else if(name == "minimal") label = tr("Minimal");
        else if(name == "none") label = tr("None");
        else if(name == "max") label = tr("Maximum");
        else if(name == "ultra") label = tr("Ultra");
		effort_combo->addItem(label, name);
		effort_combo->setItemData(effort_combo->count()-1, label, Qt::ToolTipRole);
		if(name == model.value("defaultReasoningEffort").toString()) effort_combo->setCurrentIndex(effort_combo->count()-1);
	}
	if(effort_combo->count() == 0) effort_combo->addItem(tr("Default"), QString());
}


void AIDockWidget::addImage(const QImage& image, const QString& name)
{
	if(references->count() >= 8)
	{
		QMessageBox::information(this, tr("References"), tr("You can attach up to 8 images per message."));
		return;
	}
	if(image.isNull() || !reference_directory.isValid()) return;
	const QImage resized = image.width() > 2048 || image.height() > 2048 ? image.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image;
	const QString path = reference_directory.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
	if(!resized.save(path, "PNG"))
	{
		QMessageBox::warning(this, tr("References"), tr("Could not prepare the reference image."));
		return;
	}
	// Keep normalized files until the panel is destroyed: app-server reads them asynchronously.
	QListWidgetItem* item = new QListWidgetItem(QIcon(QPixmap::fromImage(resized.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation))), name, references);
	item->setData(Qt::UserRole, path);
	item->setToolTip(name);
	references->setCurrentItem(item);
	references->show();
	remove_reference_button->show();
}

void AIDockWidget::configureMCPSettings(bool enabled, int port, const QString& directory)
{
	const QSignalBlocker check_block(enable_mcp), port_block(mcp_port);
	enable_mcp->setChecked(enabled);
	mcp_port->setValue(port);
	mcp_port->setEnabled(enabled);
	icon_directory = directory;
	updateIcons();
}

void AIDockWidget::stopCodex()
{
	codex.stop();
	send_button->setEnabled(false);
	model_combo->setEnabled(true);
	effort_combo->setEnabled(true);
	connection_label->setText(tr("Enable MCP in the AI Settings tab and connect to a world."));
}

void AIDockWidget::updateIcons()
{
	if(icon_directory.isEmpty()) return;
	const QColor colour = palette().color(QPalette::WindowText);
	tabs->setTabIcon(1, LucideIconUtils::tintedIcon(icon_directory, "settings", colour));
	LucideIconUtils::setButtonIcon(attach_button, icon_directory, "paperclip", colour);
	LucideIconUtils::setButtonIcon(remove_reference_button, icon_directory, "trash-2", colour);
	model_label->setPixmap(LucideIconUtils::tintedIcon(icon_directory, "bot", colour).pixmap(18,18));
	effort_label->setPixmap(LucideIconUtils::tintedIcon(icon_directory, "brain", colour).pixmap(18,18));
}

void AIDockWidget::setPolyHavenEnabled(bool enabled)
{
	QSignalBlocker blocker(enable_poly_haven);
	enable_poly_haven->setChecked(enabled);
}

void AIDockWidget::changeEvent(QEvent* event)
{
	QDockWidget::changeEvent(event);
	if(event->type() == QEvent::PaletteChange && !icon_directory.isEmpty()) updateIcons();
    if(event->type() == QEvent::LanguageChange && thinking_timer) retranslateUi();
}


void AIDockWidget::addImageFile(const QString& path)
{
	QImageReader reader(path);
	reader.setAutoTransform(true);
	const QSize size = reader.size();
	if(QFileInfo(path).size() > 32*1024*1024 || !size.isValid() || (qint64)size.width()*size.height() > 64000000)
	{
		QMessageBox::warning(this, tr("References"), tr("Image is invalid or too large (maximum 32 MB / 64 megapixels)."));
		return;
	}
	if(size.width() > 2048 || size.height() > 2048) reader.setScaledSize(size.scaled(2048, 2048, Qt::KeepAspectRatio));
	const QImage image = reader.read();
	if(image.isNull()) QMessageBox::warning(this, tr("References"), tr("Could not read image: %1").arg(QFileInfo(path).fileName()));
	else addImage(image, QFileInfo(path).fileName());
}


void AIDockWidget::addMimeImages(const QMimeData* data)
{
	if(data->hasImage()) addImage(qvariant_cast<QImage>(data->imageData()), tr("Clipboard"));
	else for(const QUrl& url : data->urls()) if(url.isLocalFile()) addImageFile(url.toLocalFile());
}


bool AIDockWidget::eventFilter(QObject* watched, QEvent* event)
{
	if(event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)
	{
		QDropEvent* drop = static_cast<QDropEvent*>(event);
		if(drop->mimeData()->hasImage() || drop->mimeData()->hasUrls()) { drop->acceptProposedAction(); return true; }
	}
	if(event->type() == QEvent::Drop)
	{
		QDropEvent* drop = static_cast<QDropEvent*>(event);
		if(drop->mimeData()->hasImage() || drop->mimeData()->hasUrls()) { addMimeImages(drop->mimeData()); drop->acceptProposedAction(); return true; }
	}
	if(watched == input_edit && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->matches(QKeySequence::Paste))
	{
		const QMimeData* data = QApplication::clipboard()->mimeData();
		if(data && (data->hasImage() || data->hasUrls())) { addMimeImages(data); return true; }
	}
	return QDockWidget::eventFilter(watched, event);
}


void AIDockWidget::dragEnterEvent(QDragEnterEvent* event)
{
	if(event->mimeData()->hasImage() || event->mimeData()->hasUrls()) event->acceptProposedAction();
}


void AIDockWidget::dropEvent(QDropEvent* event)
{
	addMimeImages(event->mimeData());
	event->acceptProposedAction();
}


void AIDockWidget::appendReferences(const QStringList& paths)
{
    QTextCursor cursor(conversation_view->document()); cursor.movePosition(QTextCursor::End);
    for(const QString& path:paths) {
        const QImage image(path); if(image.isNull()) continue;
        const QString id=QStringLiteral("ai-reference:")+QUuid::createUuid().toString(QUuid::WithoutBraces);
        sent_references.insert(id,path);
        const QImage thumbnail=image.scaled(200,150,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        const QUrl resource(id);
        conversation_view->document()->addResource(QTextDocument::ImageResource,resource,thumbnail);
        cursor.insertBlock();
        QTextImageFormat format; format.setName(id); format.setWidth(thumbnail.width()); format.setHeight(thumbnail.height());
        format.setAnchor(true); format.setAnchorHref(id); format.setToolTip(tr("Open reference full screen"));
        cursor.insertImage(format);
        cursor.insertBlock(); cursor.setCharFormat(QTextCharFormat());
    }
    conversation_view->setTextCursor(cursor); conversation_view->ensureCursorVisible();
}

void AIDockWidget::openReference(const QString& path)
{
    if(!QFileInfo::exists(path)) { showError(tr("Reference image is no longer available.")); return; }
    AIReferenceViewer* viewer=new AIReferenceViewer(path,window());
    viewer->winId();
    if(window()->windowHandle() && viewer->windowHandle()) viewer->windowHandle()->setScreen(window()->windowHandle()->screen());
    viewer->showFullScreen(); viewer->raise(); viewer->activateWindow();
}

void AIDockWidget::setTurnActive(bool active)
{
    turn_active=active;
    thinking_row->setVisible(active);
    if(active) { activity_phase=QStringLiteral("thinking"); thinking_elapsed.start(); thinking_timer->start(); }
    else thinking_timer->stop();
    send_button->setEnabled(codex.isReady());
    model_combo->setEnabled(!active); effort_combo->setEnabled(!active);
    updateThinkingIndicator();
}

void AIDockWidget::updateThinkingIndicator()
{
    if(!turn_active) return;
    const QString phase=activity_phase=="tool" ? tr("Working with tools…") : activity_phase=="answer" ? tr("Writing a reply…") : tr("Thinking…");
    thinking_label->setText(tr("%1 %2 s").arg(phase).arg(thinking_elapsed.elapsed()/1000));
    QPixmap frame(28,28); frame.fill(Qt::transparent);
    QPainter painter(&frame); painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(14,14); painter.rotate((thinking_elapsed.elapsed()/12)%360);
    painter.drawPixmap(-9,-9,LucideIconUtils::tintedIcon(icon_directory,"brain",palette().color(QPalette::WindowText)).pixmap(18,18));
    thinking_icon->setPixmap(frame);
}
