#include "PluginsDockWidget.h"
#include "LucideIconUtils.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDir>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>


namespace
{
QString pluginVersionKey(const QString& pluginId, const QString& version)
{
	return pluginId + QLatin1Char('@') + version;
}


QColor secondaryTextColour(const QPalette& palette, const QPalette::ColorRole foreground_role,
	const QPalette::ColorRole background_role)
{
	const QColor foreground = palette.color(foreground_role);
	const QColor background = palette.color(background_role);
	// Mid is a border/decoration role in several dark Qt styles and is too dim
	// for text. Blend normal text toward the actual surface instead of borrowing
	// Mid/Disabled so secondary labels remain readable.
	return QColor((foreground.red() * 9 + background.red()) / 10,
		(foreground.green() * 9 + background.green()) / 10,
		(foreground.blue() * 9 + background.blue()) / 10);
}


QString pluginStateText(const PluginPackageInfo& info)
{
	const auto tr = [](const char* source) { return QCoreApplication::translate("PluginsDockWidget", source); };
	QStringList states;
	states << (info.installed ? tr("Installed") : tr("Not installed"));
	states << (info.enabled ? tr("Enabled") : tr("Disabled"));
	states << (info.running ? tr("Running") : tr("Not running"));
	if(!info.runtimeError.isEmpty())
		states << tr("Error: %1").arg(info.runtimeError.left(240));
	if(!info.apiCompatible)
		states << tr("API incompatible");
	else if(info.runtime != QStringLiteral("luau") && info.runtime != QStringLiteral("wasm") && info.runtime != QStringLiteral("native-cpp"))
		states << tr("Unknown runtime: %1").arg(info.runtime);
	return states.join(QStringLiteral(" · "));
}


QString pluginRowStatusText(const PluginPackageInfo& info)
{
	const auto tr = [](const char* source) { return QCoreApplication::translate("PluginsDockWidget", source); };
	if(!info.installed) return tr("Not installed");
	if(!info.runtimeError.isEmpty()) return tr("Error");
	if(info.running) return tr("Running");
	if(info.enabled) return tr("Starting");
	return tr("Disabled");
}

QString formatPermissions(const QStringList& permissions)
{
	if(permissions.isEmpty()) return QCoreApplication::translate("PluginsDockWidget", "No permissions requested.");
	return permissions.join(QStringLiteral(", "));
}


QString catalogStateText(const PluginPackageInfo& info, const QList<PluginPackageInfo>& installed)
{
	const auto tr = [](const char* source) { return QCoreApplication::translate("PluginsDockWidget", source); };
	if(!info.apiCompatible) return tr("Incompatible with this Plugin API");
	const PluginPackageInfo* newest_installed = nullptr;
	for(const PluginPackageInfo& installed_info : installed)
		if(installed_info.id == info.id)
		{
			if(installed_info.version == info.version) return tr("Installed");
			if(!newest_installed || PluginPackageValidator::compareVersions(installed_info.version, newest_installed->version) > 0)
				newest_installed = &installed_info;
		}
	if(newest_installed)
		return PluginPackageValidator::compareVersions(info.version, newest_installed->version) > 0
			? tr("Update available") : tr("A newer version is already installed");
	return tr("Available");
}
}


PluginsDockWidget::PluginsDockWidget(QWidget* parent, PluginStoreCatalogProvider* storeProvider,
	const QString& pluginRoot, const QString& settingsRoot, const QString& iconsDirectory)
: QDockWidget(tr("Plugins"), parent),
	icons_directory(iconsDirectory),
	package_manager(pluginRoot, storeProvider, settingsRoot),
	plugin_runtime(package_manager),
	tabs(new QTabWidget(this)),
	catalog_list(new QListWidget(this)),
	installed_list(new QListWidget(this)),
	catalog_search(new QLineEdit(this)),
	catalog_status(new QLabel(this)),
	catalog_empty_label(new QLabel(this)),
	installed_help_label(new QLabel(this)),
	empty_list_label(new QLabel(this)),
	catalog_details(new QLabel(this)),
	installed_details(new QLabel(this)),
	catalog_detail_icon(new QLabel(this)),
	installed_detail_icon(new QLabel(this)),
	catalog_details_toggle(nullptr),
	installed_details_toggle(nullptr),
	catalog_details_separator(nullptr),
	installed_details_separator(nullptr),
	catalog_technical_details(nullptr),
	installed_technical_details(nullptr),
	catalog_empty_action(new QPushButton(this)),
	installed_empty_action(new QPushButton(this)),
	catalog_install_button(new QPushButton(this)),
	remove_button(new QPushButton(this)),
	installation_description(new QLabel(this)),
	installation_status(new QLabel(this)),
	instructions_browser(new QTextBrowser(this))
{
	plugin_runtime.setStateChangedCallback([this]() {
		QTimer::singleShot(0, this, [this]() { rebuildInstalledList(); });
	});
	setWidget(tabs);
	setMinimumWidth(380);
	tabs->setObjectName(QStringLiteral("pluginManagerTabs"));
	// Follow WorldSettingsWidget's native QTabWidget style; sizes are logical
	// pixels and Qt applies the active Windows high-DPI scale factor.
	tabs->setDocumentMode(false);
	tabs->setIconSize(QSize(14, 14));
	tabs->tabBar()->setExpanding(false);

	QWidget* catalog_page = new QWidget(tabs);
	QVBoxLayout* catalog_layout = new QVBoxLayout(catalog_page);
	catalog_layout->setContentsMargins(8, 8, 8, 8);
	catalog_layout->setSpacing(6);
	catalog_search->setObjectName(QStringLiteral("pluginCatalogSearch"));
	catalog_search->setClearButtonEnabled(true);
	catalog_list->setObjectName(QStringLiteral("pluginStoreList"));
	catalog_list->setIconSize(QSize(18, 18));
	catalog_status->setObjectName(QStringLiteral("pluginStoreStatus"));
	catalog_details->setObjectName(QStringLiteral("pluginCatalogDetails"));
	installed_list->setObjectName(QStringLiteral("installedPluginList"));
	installed_details->setObjectName(QStringLiteral("installedPluginDetails"));
	remove_button->setObjectName(QStringLiteral("removeSelectedPluginButton"));
	catalog_layout->addWidget(catalog_search);
	catalog_status->setWordWrap(true);
	catalog_layout->addWidget(catalog_status);
	catalog_layout->addWidget(catalog_list, 1);
	QWidget* catalog_empty = new QWidget(catalog_page);
	QVBoxLayout* catalog_empty_layout = new QVBoxLayout(catalog_empty);
	catalog_empty_layout->setContentsMargins(12, 16, 12, 16);
	catalog_empty_layout->setSpacing(6);
	QLabel* catalog_empty_icon = new QLabel(catalog_empty);
	catalog_empty_icon->setObjectName(QStringLiteral("pluginCatalogEmptyIcon"));
	catalog_empty_icon->setAlignment(Qt::AlignCenter);
	catalog_empty_icon->setPixmap(displayIcon().pixmap(44, 44));
	catalog_empty_layout->addWidget(catalog_empty_icon);
	catalog_empty_label->setWordWrap(true);
	catalog_empty_label->setAlignment(Qt::AlignCenter);
	catalog_empty_layout->addWidget(catalog_empty_label);
	catalog_empty_action->setObjectName(QStringLiteral("pluginCatalogEmptyAction"));
	catalog_empty_action->setMinimumHeight(34);
	catalog_empty_layout->addWidget(catalog_empty_action, 0, Qt::AlignHCenter);
	catalog_layout->addWidget(catalog_empty, 1);
	catalog_empty->setObjectName(QStringLiteral("pluginCatalogEmptyState"));
	catalog_details->setWordWrap(true);
	catalog_details->setTextFormat(Qt::RichText);
	catalog_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFrame* catalog_card = new QFrame(catalog_page);
	catalog_card->setObjectName(QStringLiteral("pluginCatalogDetailsCard"));
	catalog_card->setFrameShape(QFrame::StyledPanel);
	QHBoxLayout* catalog_card_layout = new QHBoxLayout(catalog_card);
	catalog_detail_icon->setObjectName(QStringLiteral("pluginCatalogDetailsIcon"));
	catalog_detail_icon->setFixedSize(28, 28);
	catalog_detail_icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
	catalog_card_layout->setContentsMargins(6, 5, 6, 5);
	catalog_card_layout->setSpacing(8);
	catalog_card_layout->addWidget(catalog_detail_icon, 0, Qt::AlignTop);
	QVBoxLayout* catalog_card_content = new QVBoxLayout();
	catalog_card_content->setContentsMargins(0, 0, 0, 0);
	catalog_card_content->setSpacing(3);
	catalog_card_content->addWidget(catalog_details);
	catalog_details_toggle = new QToolButton(catalog_card);
	catalog_details_toggle->setObjectName(QStringLiteral("catalogPluginDetailsToggle"));
	catalog_details_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	catalog_details_toggle->setArrowType(Qt::RightArrow);
	catalog_details_toggle->setCheckable(true);
	catalog_card_content->addWidget(catalog_details_toggle, 0, Qt::AlignLeft);
	catalog_details_separator = new QFrame(catalog_card);
	catalog_details_separator->setObjectName(QStringLiteral("catalogPluginDetailsSeparator"));
	catalog_details_separator->setFrameShape(QFrame::HLine);
	catalog_details_separator->setFrameShadow(QFrame::Sunken);
	catalog_technical_details = new QLabel(catalog_card);
	catalog_technical_details->setObjectName(QStringLiteral("catalogPluginTechnicalDetails"));
	catalog_technical_details->setWordWrap(true);
	catalog_technical_details->setTextFormat(Qt::RichText);
	catalog_technical_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	catalog_technical_details->setForegroundRole(QPalette::Text);
	catalog_details_separator->hide();
	catalog_technical_details->hide();
	catalog_card_content->addWidget(catalog_details_separator);
	catalog_card_content->addWidget(catalog_technical_details);
	catalog_card_layout->addLayout(catalog_card_content, 1);
	catalog_layout->addWidget(catalog_card);
	catalog_install_button->setObjectName(QStringLiteral("pluginCatalogInstallButton"));
	catalog_layout->addWidget(catalog_install_button);
	tabs->addTab(catalog_page, QString());

	QWidget* installed_page = new QWidget(tabs);
	QVBoxLayout* installed_layout = new QVBoxLayout(installed_page);
	installed_layout->setContentsMargins(8, 8, 8, 8);
	installed_layout->setSpacing(6);
	installed_help_label->setWordWrap(true);
	installed_layout->addWidget(installed_help_label);
	installed_layout->addWidget(installed_list, 1);
	QWidget* installed_empty = new QWidget(installed_page);
	installed_empty->setObjectName(QStringLiteral("installedPluginEmptyState"));
	QVBoxLayout* installed_empty_layout = new QVBoxLayout(installed_empty);
	installed_empty_layout->setContentsMargins(12, 16, 12, 16);
	installed_empty_layout->setSpacing(6);
	QLabel* installed_empty_icon = new QLabel(installed_empty);
	installed_empty_icon->setObjectName(QStringLiteral("installedPluginEmptyIcon"));
	installed_empty_icon->setAlignment(Qt::AlignCenter);
	installed_empty_icon->setPixmap(displayIcon().pixmap(44, 44));
	installed_empty_layout->addWidget(installed_empty_icon);
	empty_list_label->setWordWrap(true);
	empty_list_label->setAlignment(Qt::AlignCenter);
	installed_empty_layout->addWidget(empty_list_label);
	installed_empty_action->setObjectName(QStringLiteral("installedPluginEmptyAction"));
	installed_empty_action->setMinimumHeight(34);
	installed_empty_layout->addWidget(installed_empty_action, 0, Qt::AlignHCenter);
	installed_layout->addWidget(installed_empty, 1);
	installed_details->setWordWrap(true);
	installed_details->setTextFormat(Qt::RichText);
	installed_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFrame* installed_card = new QFrame(installed_page);
	installed_card->setObjectName(QStringLiteral("installedPluginDetailsCard"));
	installed_card->setFrameShape(QFrame::StyledPanel);
	QHBoxLayout* installed_card_layout = new QHBoxLayout(installed_card);
	installed_detail_icon->setObjectName(QStringLiteral("installedPluginDetailsIcon"));
	installed_detail_icon->setFixedSize(28, 28);
	installed_detail_icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
	installed_card_layout->setContentsMargins(6, 5, 6, 5);
	installed_card_layout->setSpacing(8);
	installed_card_layout->addWidget(installed_detail_icon, 0, Qt::AlignTop);
	QVBoxLayout* installed_card_content = new QVBoxLayout();
	installed_card_content->setContentsMargins(0, 0, 0, 0);
	installed_card_content->setSpacing(3);
	installed_card_content->addWidget(installed_details);
	installed_details_toggle = new QToolButton(installed_card);
	installed_details_toggle->setObjectName(QStringLiteral("installedPluginDetailsToggle"));
	installed_details_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	installed_details_toggle->setArrowType(Qt::RightArrow);
	installed_details_toggle->setCheckable(true);
	installed_card_content->addWidget(installed_details_toggle, 0, Qt::AlignLeft);
	installed_details_separator = new QFrame(installed_card);
	installed_details_separator->setObjectName(QStringLiteral("installedPluginDetailsSeparator"));
	installed_details_separator->setFrameShape(QFrame::HLine);
	installed_details_separator->setFrameShadow(QFrame::Sunken);
	installed_technical_details = new QLabel(installed_card);
	installed_technical_details->setObjectName(QStringLiteral("installedPluginTechnicalDetails"));
	installed_technical_details->setWordWrap(true);
	installed_technical_details->setTextFormat(Qt::RichText);
	installed_technical_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	installed_technical_details->setForegroundRole(QPalette::Text);
	installed_details_separator->hide();
	installed_technical_details->hide();
	installed_card_content->addWidget(installed_details_separator);
	installed_card_content->addWidget(installed_technical_details);
	installed_card_layout->addLayout(installed_card_content, 1);
	installed_layout->addWidget(installed_card);
	installed_layout->addWidget(remove_button);
	tabs->addTab(installed_page, QString());

	QWidget* installation_page = new QWidget(tabs);
	QVBoxLayout* installation_layout = new QVBoxLayout(installation_page);
	installation_layout->setContentsMargins(8, 8, 8, 8);
	installation_layout->setSpacing(6);
	installation_description->setWordWrap(true);
	installation_layout->addWidget(installation_description);
	QPushButton* install_button = new QPushButton(installation_page);
	install_button->setObjectName(QStringLiteral("installPluginButton"));
	installation_layout->addWidget(install_button);
	installation_status->setWordWrap(true);
	installation_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	installation_layout->addWidget(installation_status);
	installation_layout->addStretch(1);
	tabs->addTab(installation_page, QString());

	QWidget* instructions_page = new QWidget(tabs);
	QVBoxLayout* instructions_layout = new QVBoxLayout(instructions_page);
	instructions_layout->setContentsMargins(8, 8, 8, 8);
	instructions_browser->setObjectName(QStringLiteral("pluginInstructionsBrowser"));
	instructions_browser->setOpenExternalLinks(false);
	instructions_browser->setFrameShape(QFrame::NoFrame);
	instructions_layout->addWidget(instructions_browser);
	tabs->addTab(instructions_page, QString());

	connect(catalog_search, &QLineEdit::textChanged, this, &PluginsDockWidget::reloadStoreCatalog);
	connect(catalog_list, &QListWidget::currentItemChanged, this, [this]() { showSelectedPluginDetails(); });
	connect(catalog_install_button, &QPushButton::clicked, this, &PluginsDockWidget::installSelectedStorePlugin);
	connect(installed_list, &QListWidget::currentItemChanged, this, [this]() { showSelectedPluginDetails(); });
	connect(installed_list, &QListWidget::itemDoubleClicked, this, [this]() { showSelectedPluginDetails(); });
	QObject::connect(catalog_details_toggle, &QToolButton::toggled, catalog_details_toggle, [this](const bool expanded) {
		catalog_details_toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
		catalog_details_separator->setVisible(expanded);
		catalog_technical_details->setVisible(expanded);
	});
	QObject::connect(installed_details_toggle, &QToolButton::toggled, installed_details_toggle, [this](const bool expanded) {
		installed_details_toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
		installed_details_separator->setVisible(expanded);
		installed_technical_details->setVisible(expanded);
	});
	connect(remove_button, &QPushButton::clicked, this, &PluginsDockWidget::removeSelectedPlugin);
	connect(install_button, &QPushButton::clicked, this, &PluginsDockWidget::installFromFile);
	connect(catalog_empty_action, &QPushButton::clicked, this, &PluginsDockWidget::openInstallationTab);
	connect(installed_empty_action, &QPushButton::clicked, this, &PluginsDockWidget::openInstallationTab);
	connect(tabs, &QTabWidget::currentChanged, this, [this](const int index) {
		if(index == 0) reloadStoreCatalog();
		if(index == 1) rebuildInstalledList();
	});

	retranslateUi();
}


QList<PluginListEntry> PluginsDockWidget::installedPlugins() const
{
	QList<PluginListEntry> result;
	for(const PluginPackageInfo& info : package_manager.installedPlugins())
	{
		const PluginRuntimeStatus runtime_status = plugin_runtime.status(info.id, info.version);
		PluginListEntry entry;
		entry.id = info.id;
		entry.name = info.name;
		entry.version = info.version;
		entry.installed = info.installed;
		entry.enabled = runtime_status.enabled;
		entry.running = runtime_status.running;
		PluginPackageInfo display_info = info;
		display_info.enabled = runtime_status.enabled;
		display_info.running = runtime_status.running;
		display_info.runtimeError = runtime_status.error;
		entry.state = pluginStateText(display_info);
		entry.icon = displayIcon(info.icon);
		result.push_back(entry);
	}
	return result;
}


void PluginsDockWidget::setPluginHost(PluginHostInterface* host)
{
	plugin_runtime.setHost(host);
	plugin_runtime.restorePreviouslyEnabledLocalPlugins();
	rebuildInstalledList();
}


void PluginsDockWidget::shutdownPluginRuntime()
{
	plugin_runtime.shutdown();
}


#if defined(METASIBERIA_PLUGIN_TESTING)
PluginRuntimeStatus PluginsDockWidget::pluginRuntimeStatusForTest(const QString& plugin_id, const QString& version) const
{
	return plugin_runtime.status(plugin_id, version);
}

bool PluginsDockWidget::pluginRuntimeHasActiveInstanceForTest(const QString& plugin_id, const QString& version) const
{
	return plugin_runtime.hasActiveInstance(plugin_id, version);
}

int PluginsDockWidget::pluginWorkerProcessStartedCountForTest() const
{
	return plugin_runtime.workerProcessStartedCountForTest();
}

int PluginsDockWidget::pluginWorkerProcessFinishedCountForTest() const
{
	return plugin_runtime.workerProcessFinishedCountForTest();
}

void PluginsDockWidget::installPackageFileForTest(const QString& package_path)
{
	install_confirmation_requested_for_test = false;
	auto_accept_install_confirmation_for_test = true;
	installPackageFile(package_path);
}

bool PluginsDockWidget::installConfirmationWasRequestedForTest() const
{
	return install_confirmation_requested_for_test;
}
#endif


void PluginsDockWidget::showCatalogTab() { tabs->setCurrentIndex(0); }
void PluginsDockWidget::showInstalledTab() { tabs->setCurrentIndex(1); }
void PluginsDockWidget::showInstallationTab() { tabs->setCurrentIndex(2); }


QIcon PluginsDockWidget::displayIcon(const QIcon& custom_icon) const
{
	if(!custom_icon.isNull()) return custom_icon;
	const QColor colour = LucideIconUtils::themeAwareColour(QColor(QStringLiteral("#A78BFA")),
		QApplication::palette(), QPalette::WindowText, QPalette::Window);
	const QIcon lucide = LucideIconUtils::tintedIcon(icons_directory, QStringLiteral("plug"), colour, 24);
	return lucide.isNull() ? style()->standardIcon(QStyle::SP_FileIcon) : lucide;
}


void PluginsDockWidget::openInstallationTab()
{
	showInstallationTab();
	if(QPushButton* button = findChild<QPushButton*>(QStringLiteral("installPluginButton")))
		button->setFocus(Qt::OtherFocusReason);
}


void PluginsDockWidget::reloadPluginCatalogs()
{
	package_manager.reload();
	reloadStoreCatalog();
	rebuildInstalledList();
}


void PluginsDockWidget::selectPlugin(const QString& plugin_id, const QString& plugin_version)
{
	showInstalledTab();
	for(int i = 0; i < installed_list->count(); ++i)
	{
		QListWidgetItem* item = installed_list->item(i);
		if(item && item->data(Qt::UserRole).toString() == plugin_id &&
			(plugin_version.isEmpty() || item->data(Qt::UserRole + 1).toString() == plugin_version))
		{
			installed_list->setCurrentItem(item);
			showSelectedPluginDetails();
			return;
		}
	}
}


void PluginsDockWidget::retranslateUi()
{
	setWindowTitle(tr("Plugins"));
	if(toggleViewAction()) toggleViewAction()->setText(windowTitle());
	const QColor foreground = QApplication::palette().color(QPalette::WindowText);
	const QStringList icon_names = {QStringLiteral("plug"), QStringLiteral("boxes"),
		QStringLiteral("package-plus"), QStringLiteral("file-text")};
	const QStringList tab_names = {tr("Catalog"), tr("Installed"), tr("Installation"), tr("Instructions")};
	for(int i = 0; i < tabs->count(); ++i)
	{
		tabs->setTabText(i, QString());
		tabs->setTabIcon(i, LucideIconUtils::tintedIcon(icons_directory, icon_names[i], foreground, 14));
		tabs->setTabToolTip(i, tab_names[i]);
		tabs->setTabWhatsThis(i, tab_names[i]);
		tabs->widget(i)->setAccessibleName(tab_names[i]);
	}
	catalog_search->setPlaceholderText(tr("Search plugins"));
	catalog_search->setToolTip(tr("Search the published plugin catalog by name, author or category."));
	catalog_search->setAccessibleName(tr("Search plugins"));
	catalog_empty_label->setText(QStringLiteral("<h3>%1</h3><p>%2</p>")
		.arg(tr("Catalog is not connected").toHtmlEscaped(),
			tr("Published plugins will appear here when the official catalog is available.").toHtmlEscaped()));
	catalog_empty_label->setTextFormat(Qt::RichText);
	installed_help_label->setText(tr("Only explicitly trusted local Luau packages can run. Catalog packages remain inert."));
	empty_list_label->setText(QStringLiteral("<h3>%1</h3><p>%2</p>")
		.arg(tr("Your plugin shelf is empty").toHtmlEscaped(),
			tr("Install a local .mspkg package to see it here. Packages remain available after restart.").toHtmlEscaped()));
	empty_list_label->setTextFormat(Qt::RichText);
	catalog_empty_action->setText(tr("Choose a local package"));
	installed_empty_action->setText(tr("Install a plugin"));
	catalog_install_button->setText(tr("Install selected plugin"));
	remove_button->setText(tr("Remove selected plugin…"));
	installation_description->setText(tr("Choose a local .mspkg package. Before installation, Metasiberia checks the manifest, compatibility, paths, sizes, archive contents and checksums."));
	installation_status->setText(tr("Installation validates and stores files only. Enabling a local Luau package requires separate trust confirmation."));
	if(QPushButton* install_button = findChild<QPushButton*>(QStringLiteral("installPluginButton")))
		install_button->setText(tr("Install .mspkg package…"));
	const QPalette palette = QApplication::palette();
	const QString border_colour = palette.color(QPalette::Mid).name(QColor::HexRgb);
	const QString instruction_highlight_colour = palette.color(QPalette::Highlight).name(QColor::HexRgb);
	const QString alternate_colour = palette.color(QPalette::AlternateBase).name(QColor::HexRgb);
	const auto section = [&](const QString& title, const QString& body) {
		return QStringLiteral("<h3 style='margin:0.7em 0 0.2em'>%1</h3>%2<hr style='border:0;border-top:1px solid %3'/>")
			.arg(title.toHtmlEscaped(), body, border_colour);
	};
	const auto callout = [&](const QString& title, const QString& body) {
		return QStringLiteral("<div style='margin:0.5em 0;padding:0.45em 0.65em;border-left:2px solid %1;background:%2'><b>%3</b><br/>%4</div>")
			.arg(instruction_highlight_colour, alternate_colour, title.toHtmlEscaped(), body.toHtmlEscaped());
	};
	const QStringList hello_steps = {
		tr("Start from plugin_system/examples/hello-metasiberia and give your plugin a unique reverse-domain id, author and semantic version in plugin.json."),
		tr("Keep runtime set to luau and entry set to main.luau. Declare only capabilities used by the plugin, such as ui.panel and ui.notification."),
		tr("Use ui.registerPanel(viewId, title, layoutJson, onAction) for a host-rendered panel, or ui.registerCommand(id, title, callback) for a menu command. ui.showNotification(title, body) displays bounded output."),
	tr("Run python test_sdk.py, then use package_plugin.py and validate_plugin.py to create and check a local .mspkg."),
	tr("Review the source, author, requested capabilities and package hash. Install the package, then explicitly trust and enable it from Installed."),
	tr("To update, increase the semantic version and package again. Installing a newer version with the same plugin ID replaces the old version; changed contents require fresh trust.")
	};
	QString hello_steps_html = QStringLiteral("<ol style='margin-top:0;margin-bottom:0'>");
	for(const QString& step : hello_steps)
		hello_steps_html += QStringLiteral("<li>%1</li>").arg(step.toHtmlEscaped());
	hello_steps_html += QStringLiteral("</ol>");
	const QString hello_example =
		QStringLiteral("<p>%1</p><pre style='padding:0.5em;background:%2'>%3</pre>%4<p>%5</p><pre style='padding:0.5em;background:%2'>%6</pre>")
			.arg(tr("The SDK includes a working Hello Metasiberia panel example. Its entrypoint registers a host-rendered panel and sends a greeting through the notification API.").toHtmlEscaped(), alternate_colour,
				QStringLiteral("hello-metasiberia/\n  plugin.json\n  main.luau\n  assets/hello.svg").toHtmlEscaped(),
				hello_steps_html,
				tr("Example manifest:").toHtmlEscaped(),
				QStringLiteral("{\n  \"schemaVersion\": 1,\n  \"id\": \"org.metasiberia.example.hello\",\n  \"name\": \"Hello Metasiberia\",\n  \"author\": \"Metasiberia Example\",\n  \"version\": \"1.0.4\",\n  \"runtime\": \"luau\",\n  \"entry\": \"main.luau\",\n  \"api\": {\"major\": 1, \"minor\": 1},\n  \"permissions\": [\"ui.panel\", \"ui.notification\"]\n}").toHtmlEscaped(),
				tr("Panel callback excerpt from main.luau:").toHtmlEscaped(),
				QStringLiteral("ui.registerPanel(\"main\", \"Hello Metasiberia\", layout, function(action_id)\n  if action_id == \"say_hello\" then\n    ui.showNotification(\"Hello Metasiberia\", \"Привет, Метасибирь!\")\n  end\nend)").toHtmlEscaped());
	const QString instructions =
		QStringLiteral("<h2 style='margin:0 0 0.3em'>%1</h2><p style='margin:0 0 0.5em'>%2</p>")
			.arg(tr("Plugin guide").toHtmlEscaped(),
				tr("Plugins are optional packages that add features to Metasiberia through a supported extension API.").toHtmlEscaped()) +
		section(tr("What plugins are"), QStringLiteral("<p style='margin:0'>%1</p>").arg(
			tr("A .mspkg file stores a manifest and package files. Metasiberia checks its structure, compatibility and checksums before installation.").toHtmlEscaped())) +
		section(tr("Install a .mspkg package"), QStringLiteral("<ol style='margin-top:0;margin-bottom:0'><li>%1</li><li>%2</li><li>%3</li></ol>")
			.arg(tr("Open the Installation tab.").toHtmlEscaped(),
				tr("Choose Install .mspkg package and select the file.").toHtmlEscaped(),
				tr("Review the package details and confirm installation.").toHtmlEscaped())) +
		section(tr("Where an installed plugin appears"), QStringLiteral("<p style='margin:0'>%1</p>").arg(
			tr("The plugin appears in Installed and in the Plugins menu at the top of the client. Its files are kept in your per-user Metasiberia data folder.").toHtmlEscaped())) +
		section(tr("Enable or disable"), QStringLiteral("<p style='margin:0'>%1</p>").arg(
			tr("Enable executes the local Luau entrypoint after you approve its package hash. Disable removes its menu command and closes the VM.").toHtmlEscaped())) +
		section(tr("Remove a plugin"), QStringLiteral("<ol style='margin-top:0;margin-bottom:0'><li>%1</li><li>%2</li><li>%3</li></ol>")
			.arg(tr("Select it in Installed.").toHtmlEscaped(),
				tr("Choose Remove selected plugin.").toHtmlEscaped(),
				tr("Choose whether to keep or delete its separate settings.").toHtmlEscaped())) +
		callout(tr("Important"), tr("Review the publisher and requested permissions before installing packages from sources you trust.")) +
		section(tr("Create your own plugin: Hello Metasiberia"), hello_example) +
		section(tr("Current limitations"), QStringLiteral("<ul style='margin-top:0;margin-bottom:0'><li>%1</li><li>%2</li><li>%3</li><li>%4</li></ul>")
			.arg(tr("Luau runs in a supervised child process, which helps protect the client from runtime crashes; it is not an operating-system sandbox and must only run code you trust.").toHtmlEscaped(),
				tr("The online catalog and package downloads are not connected.").toHtmlEscaped(),
				tr("Plugins cannot access files, network, server data, world objects or 3D rendering.").toHtmlEscaped(),
				tr("A plugin can register one primary menu action as either a command or a host-rendered panel. Object editing, importers, network access and general 3D tools are not implemented.").toHtmlEscaped())) +
		section(tr("What comes later"), QStringLiteral("<ul style='margin-top:0;margin-bottom:0'><li>%1</li><li>%2</li><li>%3</li></ul>")
			.arg(tr("More host APIs, importer integration and expanded developer SDK tooling.").toHtmlEscaped(),
				tr("WASM and trusted native adapters, plus additional isolated tools with explicit host capabilities.").toHtmlEscaped(),
				tr("A connected catalog, updates and richer extension panels.").toHtmlEscaped())) +
		callout(tr("Important"), tr("A child process is a crash boundary, not a full sandbox. The worker has the same operating-system account permissions. Trust confirmation is tied to the installed package hash."));
	instructions_browser->setHtml(instructions);
	catalog_search->setToolTip(tr("Search the published plugin catalog by name, author or category."));
	catalog_status->setToolTip(tr("Status of the online plugin catalog."));
	// Avoid a list-wide tooltip covering the selected row and detail card. Each
	// actual package item already has its own bounded status tooltip.
	catalog_empty_action->setToolTip(tr("Open Installation to choose a local .mspkg file."));
	installed_empty_action->setToolTip(tr("Install a package from a local .mspkg file."));
	catalog_install_button->setToolTip(tr("Install the selected catalog package when a download source is available."));
	remove_button->setToolTip(tr("Remove the selected package and choose whether to keep its settings."));
	installation_description->setToolTip(tr("Package contents are checked before they are copied into your plugin folder."));
	installation_status->setToolTip(tr("Package installation never starts code. Enable the package separately after reviewing its source and hash."));
	instructions_browser->setToolTip(tr("Read about package installation, plugin state and current limitations."));
	if(QPushButton* install_button = findChild<QPushButton*>(QStringLiteral("installPluginButton")))
		install_button->setToolTip(tr("Choose a local .mspkg file to validate and install."));
	if(!icons_directory.isEmpty())
	{
		LucideIconUtils::setButtonIcon(catalog_empty_action, icons_directory, QStringLiteral("package-open"), foreground, 18);
		LucideIconUtils::setButtonIcon(installed_empty_action, icons_directory, QStringLiteral("package-plus"), foreground, 18);
		LucideIconUtils::setButtonIcon(catalog_install_button, icons_directory, QStringLiteral("package-plus"), foreground, 18);
		LucideIconUtils::setButtonIcon(remove_button, icons_directory, QStringLiteral("trash-2"), foreground, 18);
		if(QPushButton* install_button = findChild<QPushButton*>(QStringLiteral("installPluginButton")))
			LucideIconUtils::setButtonIcon(install_button, icons_directory, QStringLiteral("package-plus"), foreground, 18);
	}
	// The World Settings panel uses Qt's active application style and palette.
	// Keep the plugin tabs, forms, buttons and frames native as well; only soften
	// list selection using the existing palette roles rather than a second theme.
	QPalette list_palette = QApplication::palette();
	const QColor base_colour = list_palette.color(QPalette::Base);
	const QColor highlight_colour = list_palette.color(QPalette::Highlight);
	const QColor selected_colour(
		(base_colour.red() * 4 + highlight_colour.red()) / 5,
		(base_colour.green() * 4 + highlight_colour.green()) / 5,
		(base_colour.blue() * 4 + highlight_colour.blue()) / 5);
	for(const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
	{
		list_palette.setColor(group, QPalette::Highlight, selected_colour);
		list_palette.setColor(group, QPalette::HighlightedText, list_palette.color(QPalette::Active, QPalette::Text));
	}
	for(QListWidget* list : {catalog_list, installed_list})
	{
		list->setPalette(list_palette);
		list->setAlternatingRowColors(true);
		list->setSelectionBehavior(QAbstractItemView::SelectRows);
		list->setMouseTracking(true);
	}
	rebuildCatalogList();
	rebuildInstalledList();
	reloadStoreCatalog();
}


void PluginsDockWidget::reloadStoreCatalog()
{
	QString error;
	catalog_entries = package_manager.storePlugins(catalog_search->text(), error);
	const QString status = package_manager.storeStatusText();
	if(!error.isEmpty()) catalog_status->setText(error);
	else if(status == QStringLiteral("The plugin store service is not configured. No published plugins are available."))
		catalog_status->setText(tr("The plugin store service is not configured. No published plugins are available."));
	else catalog_status->setText(status);
	rebuildCatalogList();
}


void PluginsDockWidget::rebuildCatalogList()
{
	catalog_list->clear();
	catalog_details->clear();
	catalog_details->parentWidget()->setVisible(false);
	catalog_detail_icon->clear();
	const QList<PluginPackageInfo> installed = package_manager.installedPlugins();
	for(const PluginPackageInfo& info : catalog_entries)
	{
		const QString compatibility = info.apiCompatible ? tr("API compatible") : tr("API incompatible");
		const QString categories = info.categories.isEmpty() ? tr("Uncategorized") : info.categories.join(QStringLiteral(" · "));
		const QString status = catalogStateText(info, installed);
		QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1\n%2  ·  %3\n%4\n%5  ·  %6")
			.arg(info.name, info.author, info.version, categories, compatibility, status), catalog_list);
		item->setIcon(displayIcon(info.icon));
		item->setToolTip(tr("%1 — %2, version %3. Select for details.").arg(info.name, info.author, info.version));
		item->setSizeHint(QSize(180, 72));
		item->setData(Qt::UserRole, info.id);
	}
	catalog_empty_label->setVisible(catalog_entries.isEmpty());
	catalog_empty_label->parentWidget()->setVisible(catalog_entries.isEmpty());
	catalog_list->setVisible(!catalog_entries.isEmpty());
	updateCatalogActions();
}


void PluginsDockWidget::updateCatalogActions()
{
	const QListWidgetItem* item = catalog_list->currentItem();
	if(!item)
	{
		catalog_install_button->setEnabled(false);
		catalog_install_button->setVisible(false);
		catalog_install_button->setText(tr("Install selected plugin"));
		return;
	}
	const QString id = item->data(Qt::UserRole).toString();
	for(const PluginPackageInfo& info : catalog_entries)
		if(info.id == id)
		{
			const QString state = catalogStateText(info, package_manager.installedPlugins());
			const bool is_available = state == tr("Available");
			catalog_install_button->setVisible(true);
			catalog_install_button->setText(state == tr("Update available") ? tr("Updates are not available yet") : tr("Install selected plugin"));
			catalog_install_button->setEnabled(is_available && package_manager.canDownloadStorePackage(info));
			return;
		}
	catalog_install_button->setEnabled(false);
	catalog_install_button->setVisible(false);
}


void PluginsDockWidget::rebuildInstalledList()
{
	const QString previous_id = selected_plugin_id;
	for(int row_index = installed_list->count() - 1; row_index >= 0; --row_index)
	{
		QListWidgetItem* const old_item = installed_list->item(row_index);
		if(QWidget* const old_row = installed_list->itemWidget(old_item))
		{
			installed_list->removeItemWidget(old_item);
			delete old_row;
		}
		delete installed_list->takeItem(row_index);
	}
	const QList<PluginPackageInfo> infos = package_manager.installedPlugins();
	const bool has_plugins = !infos.isEmpty();
	installed_help_label->setVisible(has_plugins);
	installed_list->setVisible(has_plugins);
	empty_list_label->parentWidget()->setVisible(!has_plugins);
	remove_button->setEnabled(false);
	remove_button->setVisible(false);
	for(const PluginPackageInfo& info : infos)
	{
		PluginPackageInfo display_info = info;
		const PluginRuntimeStatus runtime_status = plugin_runtime.status(info.id, info.version);
		display_info.enabled = runtime_status.enabled;
		display_info.running = runtime_status.running;
		display_info.runtimeError = runtime_status.error;
		QListWidgetItem* item = new QListWidgetItem(installed_list);
		item->setData(Qt::UserRole, display_info.id);
		item->setData(Qt::UserRole + 1, display_info.version);
		item->setToolTip(tr("%1, version %2. %3.").arg(display_info.name, display_info.version, pluginStateText(display_info)));
		item->setSizeHint(QSize(220, 46));
		QWidget* row = new QWidget(installed_list);
		row->setObjectName(QStringLiteral("installedPluginRow"));
		row->setAttribute(Qt::WA_StyledBackground, false);
		QHBoxLayout* row_layout = new QHBoxLayout(row);
		row_layout->setContentsMargins(4, 2, 4, 2);
		row_layout->setSpacing(6);
		QLabel* plugin_icon = new QLabel(row);
		plugin_icon->setObjectName(QStringLiteral("installedPluginRowIcon"));
		plugin_icon->setFixedSize(22, 22);
		plugin_icon->setAlignment(Qt::AlignCenter);
		plugin_icon->setPixmap(displayIcon(display_info.icon).pixmap(18, 18));
		row_layout->addWidget(plugin_icon);
		QVBoxLayout* text_layout = new QVBoxLayout();
		text_layout->setContentsMargins(0, 0, 0, 0);
		text_layout->setSpacing(2);
		QLabel* name_label = new QLabel(display_info.name, row);
		name_label->setObjectName(QStringLiteral("installedPluginName"));
		QFont name_font = name_label->font();
		name_font.setWeight(QFont::DemiBold);
		name_label->setFont(name_font);
		QLabel* status_label = new QLabel(tr("Version %1 · %2").arg(display_info.version, pluginRowStatusText(display_info)), row);
		status_label->setObjectName(QStringLiteral("installedPluginStatus"));
		status_label->setWordWrap(true);
		status_label->setToolTip(display_info.runtimeError.isEmpty() ? pluginStateText(display_info) : display_info.runtimeError);
		QFont status_font = status_label->font();
		if(status_font.pointSizeF() > 1.0) status_font.setPointSizeF(status_font.pointSizeF() - 1.0);
		status_label->setFont(status_font);
		status_label->setForegroundRole(QPalette::Text);
		QPalette status_palette = status_label->palette();
		const QColor secondary = secondaryTextColour(QApplication::palette(), QPalette::Text, QPalette::Base);
		for(const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
			status_palette.setColor(group, QPalette::Text, secondary);
		status_label->setPalette(status_palette);
		text_layout->addWidget(name_label);
		text_layout->addWidget(status_label);
		row_layout->addLayout(text_layout, 1);
		QCheckBox* enabled_switch = new QCheckBox(row);
		enabled_switch->setObjectName(QStringLiteral("pluginEnabledSwitch"));
		enabled_switch->setStyleSheet(QStringLiteral(
			"QCheckBox#pluginEnabledSwitch { color: #E8E8EA; spacing: 6px; }"
			"QCheckBox#pluginEnabledSwitch:disabled { color: #A0A0A6; }"
			"QCheckBox#pluginEnabledSwitch::indicator { width: 14px; height: 14px; border: 1px solid #A8A8AD; border-radius: 3px; background: #202024; }"
			"QCheckBox#pluginEnabledSwitch::indicator:hover { border-color: #C4A7FF; }"
			"QCheckBox#pluginEnabledSwitch::indicator:checked { background: #8B5CF6; border-color: #C4A7FF; }"
			"QCheckBox#pluginEnabledSwitch::indicator:disabled { border-color: #74747A; background: #29292E; }"));
		enabled_switch->setChecked(display_info.enabled);
		enabled_switch->setText(display_info.enabled ? tr("Enabled") : tr("Disabled"));
		const bool can_run = plugin_runtime.hasHost() && display_info.source == QStringLiteral("Local package") &&
			display_info.runtime == QStringLiteral("luau") && display_info.apiCompatible;
		const bool removal_pending = pending_removals.contains(pluginVersionKey(display_info.id, display_info.version));
		const bool confirmation_pending = pending_enable_confirmations.contains(pluginVersionKey(display_info.id, display_info.version));
		enabled_switch->setEnabled(can_run && !removal_pending && !confirmation_pending);
		enabled_switch->setToolTip(confirmation_pending
			? tr("Waiting for explicit trust confirmation.")
			: removal_pending
			? tr("Removal is waiting for the plugin process to stop.")
			: !can_run
			? (!plugin_runtime.hasHost() ? tr("The Plugin Runtime host is unavailable.") :
				display_info.source != QStringLiteral("Local package")
					? tr("Only explicitly trusted local packages can run in this prototype.")
					: display_info.runtime != QStringLiteral("luau")
						? tr("This package uses an unsupported runtime.")
						: tr("This package is incompatible with the current Plugin API."))
			: display_info.enabled
				? tr("Disable this plugin and remove its registered command.")
				: tr("Run this local Luau package after explicit trust confirmation."));
		enabled_switch->setAccessibleName(display_info.running ? tr("Plugin is running") :
			display_info.enabled ? tr("Plugin is enabled but not running") : tr("Plugin is disabled and not running"));
		connect(enabled_switch, &QCheckBox::toggled, this,
			[this, plugin_id = display_info.id, version = display_info.version](const bool enabled) {
				qInfo() << "Plugin UI toggle requested" << plugin_id << version << enabled;
				setPluginEnabled(plugin_id, version, enabled);
			});
		row_layout->addWidget(enabled_switch);
		row->setToolTip(item->toolTip());
		installed_list->setItemWidget(item, row);
		if(info.id == previous_id) installed_list->setCurrentItem(item);
	}
	if(installed_list->currentItem()) showSelectedPluginDetails();
	else
	{
		installed_details->clear();
		installed_details->parentWidget()->setVisible(false);
		installed_detail_icon->clear();
		installed_technical_details->clear();
		installed_details_toggle->setChecked(false);
	}
	selected_plugin_id = installed_list->currentItem() ? installed_list->currentItem()->data(Qt::UserRole).toString() : QString();
	emit installedPluginsChanged();
}


void PluginsDockWidget::scheduleInstalledListRebuild()
{
	QTimer::singleShot(0, this, [this]() { rebuildInstalledList(); });
}


void PluginsDockWidget::showSelectedPluginDetails()
{
	if(tabs->currentIndex() == 0)
	{
		const QString id = catalog_list->currentItem() ? catalog_list->currentItem()->data(Qt::UserRole).toString() : QString();
		for(const PluginPackageInfo& info : catalog_entries)
			if(info.id == id)
			{
				showPackageDetails(info, true);
				updateCatalogActions();
				return;
			}
		updateCatalogActions();
		return;
	}
	const QListWidgetItem* item = installed_list->currentItem();
	if(!item)
	{
		remove_button->setEnabled(false);
		remove_button->setVisible(false);
		installed_details->clear();
		installed_details->parentWidget()->setVisible(false);
		installed_detail_icon->clear();
		return;
	}
	const QString id = item->data(Qt::UserRole).toString();
	const QString version = item->data(Qt::UserRole + 1).toString();
	for(const PluginPackageInfo& info : package_manager.installedPlugins())
		if(info.id == id && info.version == version)
		{
			selected_plugin_id = id;
			remove_button->setEnabled(!pending_removals.contains(pluginVersionKey(id, version)));
			remove_button->setVisible(true);
			showPackageDetails(info, false);
			return;
		}
}


void PluginsDockWidget::showPackageDetails(const PluginPackageInfo& info, const bool is_store_entry)
{
	PluginPackageInfo display_info = info;
	if(!is_store_entry)
	{
		const PluginRuntimeStatus runtime_status = plugin_runtime.status(info.id, info.version);
		display_info.enabled = runtime_status.enabled;
		display_info.running = runtime_status.running;
		display_info.runtimeError = runtime_status.error;
	}
	const PluginPackageInfo& shown = display_info;
	const QString compatibility = shown.apiCompatible
		? tr("API compatibility: supported by this client")
		: tr("API compatibility: package requires %1.%2; client supports %3.%4")
			.arg(shown.apiMajor).arg(shown.apiMinor).arg(PluginPackageValidator::CurrentApiMajor).arg(PluginPackageValidator::CurrentApiMinor);
	const QString state = is_store_entry ? catalogStateText(shown, package_manager.installedPlugins()) : pluginStateText(shown);
	QLabel* const details = is_store_entry ? catalog_details : installed_details;
	QLabel* const icon_label = is_store_entry ? catalog_detail_icon : installed_detail_icon;
	QToolButton* const details_toggle = is_store_entry ? catalog_details_toggle : installed_details_toggle;
	QFrame* const details_separator = is_store_entry ? catalog_details_separator : installed_details_separator;
	QLabel* const technical_details = is_store_entry ? catalog_technical_details : installed_technical_details;
	details->parentWidget()->setVisible(true);
	icon_label->setPixmap(displayIcon(shown.icon).pixmap(24, 24));
	const QString categories = shown.categories.isEmpty() ? tr("Uncategorized") : shown.categories.join(QStringLiteral(" · "));
	const auto field = [](const QString& label, const QString& value, const QString& muted_colour) {
		return QStringLiteral("<p style='margin:2px 0'><span style='color:%1'>%2:</span> %3</p>")
			.arg(muted_colour, label.toHtmlEscaped(), value.toHtmlEscaped());
	};
	const QPalette palette = QApplication::palette();
	const QColor accent = LucideIconUtils::themeAwareColour(palette.color(QPalette::Highlight),
		palette, QPalette::Highlight, QPalette::Window);
	const QString muted_colour = secondaryTextColour(palette, QPalette::WindowText, QPalette::Window).name(QColor::HexRgb);
	const QString title = QStringLiteral("<p style='margin:0'><span style='font-size:1.1em;font-weight:600;color:%1'>%2</span><br/><span style='color:%3'>%4: %5 &nbsp;·&nbsp; %6: %7</span></p>")
		.arg(accent.name(QColor::HexRgb), shown.name.toHtmlEscaped(), muted_colour,
			tr("Version").toHtmlEscaped(), shown.version.toHtmlEscaped(), tr("Author").toHtmlEscaped(),
			tr("%1 (manifest value; not verified)").arg(shown.author).toHtmlEscaped());
	const QString description = QStringLiteral("<p style='margin:3px 0'>%1</p>")
		.arg((shown.description.isEmpty() ? tr("No description provided.") : shown.description).toHtmlEscaped());
	const QString summary = title + description +
		field(tr("Status"), state, muted_colour) + QStringLiteral("<hr/>") +
		field(tr("Compatibility"), compatibility, muted_colour) +
		field(tr("Permissions"), formatPermissions(shown.permissions), muted_colour);
	details->setText(summary);
	const QString source = is_store_entry ? tr("Plugin catalog") :
		(shown.source.isEmpty() ? tr("Local package") : shown.source);
	const QString installed_at = shown.installedAt.isValid()
		? shown.installedAt.toLocalTime().toString(Qt::DefaultLocaleShortDate) : tr("Not available");
	technical_details->setText(
		field(tr("Category"), categories, muted_colour) +
		field(tr("Source"), source, muted_colour) +
		field(tr("Runtime"), shown.runtime, muted_colour) +
		field(tr("Plugin ID"), shown.id, muted_colour) +
		field(tr("Installed"), is_store_entry ? tr("No") : installed_at, muted_colour));
	if(!is_store_entry && !shown.runtimeError.isEmpty())
		details->setText(details->text() + QStringLiteral("<p style='color:#ff8888'><b>%1</b></p>").arg(shown.runtimeError.toHtmlEscaped()));
	details_toggle->setText(tr("Technical details"));
	details_toggle->setToolTip(tr("Expand or collapse technical package information."));
	details_toggle->setChecked(false);
	details_separator->setVisible(false);
	technical_details->setVisible(false);
	details->setToolTip(tr("Details are read from the package manifest and are not independently verified."));
}


void PluginsDockWidget::setPluginEnabled(const QString& plugin_id, const QString& plugin_version, const bool enabled)
{
	QElapsedTimer elapsed;
	elapsed.start();
	qInfo() << "Plugin UI toggle handler entered" << plugin_id << plugin_version << enabled;
	QString error;
	if(!enabled)
	{
		if(!plugin_runtime.disable(plugin_id, plugin_version, error))
			qWarning() << "Plugin disable failed:" << error;
		scheduleInstalledListRebuild();
		qInfo() << "Plugin UI toggle handler returned" << plugin_id << plugin_version << "disable" << elapsed.elapsed() << "ms";
		return;
	}

	PluginPackageInfo info;
	bool found = false;
	for(const PluginPackageInfo& candidate : package_manager.installedPlugins())
		if(candidate.id == plugin_id && candidate.version == plugin_version)
		{
			info = candidate;
			found = true;
			break;
		}
	if(!found)
	{
		qWarning() << "Plugin enable failed: selected package is no longer installed" << plugin_id << plugin_version;
		scheduleInstalledListRebuild();
		qInfo() << "Plugin UI toggle handler returned" << plugin_id << plugin_version << "missing-package" << elapsed.elapsed() << "ms";
		return;
	}

	const bool needs_trust = plugin_runtime.requiresExplicitTrust(plugin_id, plugin_version);
	if(needs_trust)
	{
		const QString confirmation_key = pluginVersionKey(plugin_id, plugin_version);
		if(pending_enable_confirmations.contains(confirmation_key))
			return;
		pending_enable_confirmations.insert(confirmation_key);
		scheduleInstalledListRebuild();
		auto* const prompt = new QDialog(this);
		prompt->setObjectName(QStringLiteral("pluginTrustDialog"));
		prompt->setAttribute(Qt::WA_DeleteOnClose);
		prompt->setWindowModality(Qt::WindowModal);
		prompt->setWindowTitle(tr("Trust local plugin code"));
		QVBoxLayout* const prompt_layout = new QVBoxLayout(prompt);
	QLabel* const prompt_details = new QLabel(tr("Run %1 version %2?\n\nThis Luau code runs in a supervised child process so a worker crash can be isolated from the client. It is not an OS sandbox: the process has the same account permissions. The package may register one primary menu action: a host-rendered panel or a legacy command. Panel controls are host-owned Qt widgets; Luau receives bounded action IDs over IPC. The only scene access available is the explicitly requested read-only scene summary; there is no filesystem, network, object editing, avatar control, server authority or arbitrary 3D API.\n\nAuthor in manifest (not verified): %3\nPackage SHA-256: %4\nVerified installed-content SHA-256: %5\nRequested capabilities: %6\n\nTrust this exact local package content and enable it?")
			.arg(info.name, info.version, info.author, QString::fromLatin1(info.packageSha256.toHex()),
				QString::fromLatin1(info.installedContentSha256.toHex()), formatPermissions(info.permissions)), prompt);
		prompt_details->setWordWrap(true);
		prompt_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
		prompt_layout->addWidget(prompt_details);
		QDialogButtonBox* const prompt_buttons = new QDialogButtonBox(prompt);
		QPushButton* const trust_button = prompt_buttons->addButton(QDialogButtonBox::Ok);
		trust_button->setText(tr("Trust and Enable"));
		prompt_buttons->addButton(QDialogButtonBox::Cancel);
		prompt_layout->addWidget(prompt_buttons);
		connect(prompt_buttons, &QDialogButtonBox::accepted, prompt, &QDialog::accept);
		connect(prompt_buttons, &QDialogButtonBox::rejected, prompt, &QDialog::reject);
		const QByteArray reviewed_content_hash = info.installedContentSha256;
		connect(prompt, &QDialog::finished, this,
			[this, plugin_id, plugin_version, confirmation_key, reviewed_content_hash](const int result) {
				pending_enable_confirmations.remove(confirmation_key);
				if(result == QDialog::Accepted)
				{
					QByteArray current_content_hash;
					for(const PluginPackageInfo& installed : package_manager.installedPlugins())
						if(installed.id == plugin_id && installed.version == plugin_version)
						{
							current_content_hash = installed.installedContentSha256;
							break;
						}
					QString error;
					if(current_content_hash == reviewed_content_hash)
					{
						if(!plugin_runtime.enable(plugin_id, plugin_version, true, error) && !error.isEmpty())
							qWarning() << "Plugin enable failed:" << error;
					}
					else
					{
						// The reviewed identity changed while confirmation was open. Never
						// transfer consent to a different installed package hash.
						plugin_runtime.enable(plugin_id, plugin_version, false, error);
						qWarning() << "Plugin trust was not applied because installed content changed" << plugin_id << plugin_version;
					}
				}
				rebuildInstalledList();
			});
		prompt->open();
		qInfo() << "Plugin UI toggle handler returned" << plugin_id << plugin_version << "trust-prompt-open" << elapsed.elapsed() << "ms";
		return;
	}

	if(!plugin_runtime.enable(plugin_id, plugin_version, needs_trust, error))
		qWarning() << "Plugin enable failed:" << error;
	scheduleInstalledListRebuild();
	qInfo() << "Plugin UI toggle handler returned" << plugin_id << plugin_version << "enable" << elapsed.elapsed() << "ms";
}


void PluginsDockWidget::installFromFile()
{
	const QString package_path = QFileDialog::getOpenFileName(this, tr("Select plugin package"), QString(),
		tr("Metasiberia plugin packages (*.mspkg)"));
	if(package_path.isEmpty()) return;
	installPackageFile(package_path);
}


void PluginsDockWidget::installSelectedStorePlugin()
{
	const QListWidgetItem* item = catalog_list->currentItem();
	if(!item) return;
	const QString id = item->data(Qt::UserRole).toString();
	for(const PluginPackageInfo& catalog_info : catalog_entries)
		if(catalog_info.id == id)
		{
			if(!package_manager.canDownloadStorePackage(catalog_info))
			{
				QMessageBox::information(this, tr("Plugin catalog"), tr("The publication catalog has no package download service. You can choose a local package in the Installation tab."));
				return;
			}
			QTemporaryDir download_directory(QDir(QDir::tempPath()).filePath(QStringLiteral("metasiberia-plugin-XXXXXX")));
			if(!download_directory.isValid())
			{
				QMessageBox::warning(this, tr("Plugin catalog"), tr("A temporary package download directory could not be created."));
				return;
			}
			const QString package_path = download_directory.filePath(QStringLiteral("catalog.mspkg"));
			QString error;
			if(!package_manager.downloadStorePackage(catalog_info, package_path, error))
			{
				QMessageBox::warning(this, tr("Plugin catalog"), tr("Catalog download failed: %1").arg(error));
				return;
			}
			PluginPackageInfo downloaded_info;
			if(!package_manager.inspectPackage(package_path, downloaded_info, error))
			{
				QMessageBox::warning(this, tr("Plugin catalog"), tr("The downloaded package does not match the selected catalog entry: %1").arg(error));
				return;
			}
			if(downloaded_info.id != catalog_info.id || downloaded_info.version != catalog_info.version)
			{
				QMessageBox::warning(this, tr("Plugin catalog"), tr("The downloaded package id or version differs from the selected catalog entry."));
				return;
			}
			installPackageFile(package_path, QStringLiteral("Plugin catalog"));
			return;
		}
}


void PluginsDockWidget::installPackageFile(const QString& package_path, const QString& source)
{
	PluginPackageInfo info;
	QString error;
	if(!package_manager.inspectPackage(package_path, info, error))
	{
		installation_status->setText(tr("Package rejected: %1").arg(error));
		QMessageBox::warning(this, tr("Package validation"), installation_status->text());
		return;
	}
	if(!info.apiCompatible)
	{
		installation_status->setText(tr("The package is valid but incompatible with this Plugin API version. It was not installed."));
		QMessageBox::warning(this, tr("Incompatible package"), installation_status->text());
		return;
	}
	bool replacing_existing = false;
	for(const PluginPackageInfo& existing : package_manager.installedPlugins())
		if(existing.id == info.id)
		{
			replacing_existing = true;
			if(plugin_runtime.hasActiveInstance(existing.id, existing.version))
			{
				QString stop_error;
				plugin_runtime.disable(existing.id, existing.version, stop_error);
				installation_status->setText(tr("The previous version is stopping. Wait until it exits, then retry the update."));
				rebuildInstalledList();
				return;
			}
		}
	const QString review = tr("Name: %1\nId: %2\nAuthor (unverified manifest value): %3\nVersion: %4\nRuntime: %5\nPlugin API: %6.%7\nDeclared capabilities: %8\n\nInstallation validates and stores files only. Code will not run until you explicitly enable a trusted local Luau package.")
		.arg(info.name, info.id, info.author, info.version, info.runtime)
		.arg(info.apiMajor).arg(info.apiMinor).arg(formatPermissions(info.permissions));
	if(info.runtime != QStringLiteral("luau") && info.runtime != QStringLiteral("wasm") && info.runtime != QStringLiteral("native-cpp"))
		installation_status->setText(tr("Unknown runtime '%1'. The package can only be stored as inert files.").arg(info.runtime));
	else
	installation_status->setText(replacing_existing
		? tr("Package validated. Installing it will atomically replace older installed versions; execution remains disabled until you review and trust it.")
		: tr("Package validated. Installation remains inert; enabling this local Luau package requires a separate trust confirmation."));
#if defined(METASIBERIA_PLUGIN_TESTING)
	if(auto_accept_install_confirmation_for_test)
	{
		auto_accept_install_confirmation_for_test = false;
		install_confirmation_requested_for_test = true;
	}
	else
#endif
	{
		QMessageBox confirm(this);
		confirm.setWindowTitle(tr("Confirm installation"));
		confirm.setText(review);
		QPushButton* install_button = confirm.addButton(tr("Install"), QMessageBox::AcceptRole);
		confirm.addButton(QMessageBox::Cancel);
		confirm.exec();
		if(confirm.clickedButton() != install_button)
			return;
	}
	PluginPackageInfo installed;
	if(!package_manager.installPackage(package_path, installed, error, info.packageSha256, source))
	{
		installation_status->setText(tr("Installation failed: %1").arg(error));
		QMessageBox::warning(this, tr("Plugin installation"), installation_status->text());
		return;
	}
	installation_status->setText(replacing_existing
		? tr("Updated to %1 version %2. Review and explicitly trust the new package contents before enabling.").arg(installed.name, installed.version)
		: tr("Package %1 version %2 is installed and disabled. Review it before enabling.").arg(installed.name, installed.version));
	rebuildInstalledList();
	selectPlugin(installed.id, installed.version);
}


void PluginsDockWidget::removeSelectedPlugin()
{
	const QListWidgetItem* item = installed_list->currentItem();
	if(!item) return;
	const QString id = item->data(Qt::UserRole).toString();
	const QString version = item->data(Qt::UserRole + 1).toString();
	const QString key = pluginVersionKey(id, version);
	if(pending_removals.contains(key)) return;
	const PluginRuntimeStatus runtime_status = plugin_runtime.status(id, version);
	const bool active_instance = plugin_runtime.hasActiveInstance(id, version);
	QMessageBox prompt(this);
	prompt.setWindowTitle(tr("Remove plugin"));
	prompt.setText(runtime_status.running || active_instance
		? tr("Remove %1 version %2? It will be stopped before its files are removed. Choose separately what to do with its user settings.").arg(id, version)
		: tr("Remove %1 version %2? Choose separately what to do with its user settings.").arg(id, version));
	QPushButton* keep_settings = prompt.addButton(tr("Remove; keep settings"), QMessageBox::DestructiveRole);
	QPushButton* delete_settings = prompt.addButton(tr("Remove and delete settings"), QMessageBox::DestructiveRole);
	prompt.addButton(QMessageBox::Cancel);
	prompt.exec();
	if(prompt.clickedButton() != keep_settings && prompt.clickedButton() != delete_settings) return;
	const bool remove_settings = prompt.clickedButton() == delete_settings;
	QString error;
	if(!plugin_runtime.disable(id, version, error))
	{
		QMessageBox::warning(this, tr("Plugin removal"), tr("The plugin could not be stopped safely, so it was not removed: %1").arg(error));
		return;
	}
	if(plugin_runtime.hasActiveInstance(id, version))
	{
		pending_removals.insert(key);
		installation_status->setText(tr("Removal is waiting for the plugin process to stop."));
		remove_button->setEnabled(false);
		rebuildInstalledList();
		const qint64 deadline_ms = QDateTime::currentMSecsSinceEpoch() + 7000;
		QTimer* const removal_timer = new QTimer(this);
		removal_timer->setInterval(50);
		connect(removal_timer, &QTimer::timeout, this,
			[this, removal_timer, id, version, key, remove_settings, deadline_ms]() {
				if(plugin_runtime.hasActiveInstance(id, version))
				{
					if(QDateTime::currentMSecsSinceEpoch() < deadline_ms) return;
					removal_timer->stop();
					removal_timer->deleteLater();
					pending_removals.remove(key);
					installation_status->setText(tr("The plugin process did not stop in time. Its files were left in place."));
					QMessageBox::warning(this, tr("Plugin removal"), installation_status->text());
					rebuildInstalledList();
					return;
				}
				removal_timer->stop();
				removal_timer->deleteLater();
				QString removal_error;
				if(!package_manager.removePackage(id, version, remove_settings, removal_error))
				{
					pending_removals.remove(key);
					QMessageBox::warning(this, tr("Plugin removal"), removal_error);
					rebuildInstalledList();
					return;
				}
				pending_removals.remove(key);
				selected_plugin_id.clear();
				installed_details->clear();
				rebuildInstalledList();
			});
		removal_timer->start();
		return;
	}
	if(!package_manager.removePackage(id, version, remove_settings, error))
	{
		QMessageBox::warning(this, tr("Plugin removal"), error);
		return;
	}
	selected_plugin_id.clear();
	installed_details->clear();
	rebuildInstalledList();
}
