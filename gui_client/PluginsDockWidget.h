#pragma once

#include "PluginPackage.h"
#include "PluginRuntime.h"

#include <QtCore/QList>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtGui/QIcon>
#include <QtWidgets/QDockWidget>


class QLabel;
class QFrame;
class QListWidget;
class QLineEdit;
class QPushButton;
class QToolButton;
class QTabWidget;
class QTextBrowser;


struct PluginListEntry
{
	QString id;
	QString name;
	QString version;
	bool installed = false;
	bool enabled = false;
	bool running = false;
	QString state;
	QIcon icon;
};


class PluginsDockWidget final : public QDockWidget
{
	Q_OBJECT
public:
	explicit PluginsDockWidget(QWidget* parent = nullptr, PluginStoreCatalogProvider* storeProvider = nullptr,
		const QString& pluginRoot = QString(), const QString& settingsRoot = QString(),
		const QString& iconsDirectory = QString());

	QList<PluginListEntry> installedPlugins() const;
	void showCatalogTab();
	void showInstalledTab();
	void showInstallationTab();
	void selectPlugin(const QString& plugin_id, const QString& plugin_version = QString());
	void reloadPluginCatalogs();
	void retranslateUi();
	void setPluginHost(PluginHostInterface* host);
	void shutdownPluginRuntime();
#if defined(METASIBERIA_PLUGIN_TESTING)
	PluginRuntimeStatus pluginRuntimeStatusForTest(const QString& plugin_id, const QString& version) const;
	bool pluginRuntimeHasActiveInstanceForTest(const QString& plugin_id, const QString& version) const;
	int pluginWorkerProcessStartedCountForTest() const;
	int pluginWorkerProcessFinishedCountForTest() const;
	void installPackageFileForTest(const QString& package_path);
	bool installConfirmationWasRequestedForTest() const;
#endif

signals:
	void installedPluginsChanged();

private slots:
	void reloadStoreCatalog();
	void installFromFile();
	void installSelectedStorePlugin();
	void removeSelectedPlugin();
	void showSelectedPluginDetails();
	void setPluginEnabled(const QString& plugin_id, const QString& plugin_version, bool enabled);

private:
	void installPackageFile(const QString& packagePath, const QString& source = QStringLiteral("Local package"));
	QIcon displayIcon(const QIcon& custom_icon = QIcon()) const;
	void openInstallationTab();
	void updateCatalogActions();
	void rebuildInstalledList();
	void scheduleInstalledListRebuild();
	void rebuildCatalogList();
	void showPackageDetails(const PluginPackageInfo& info, bool is_store_entry);
	QString icons_directory;
	PluginPackageManager package_manager;
	PluginRuntime plugin_runtime;
	QTabWidget* tabs;
	QListWidget* catalog_list;
	QListWidget* installed_list;
	QLineEdit* catalog_search;
	QLabel* catalog_status;
	QLabel* catalog_empty_label;
	QLabel* installed_help_label;
	QLabel* empty_list_label;
	QLabel* catalog_details;
	QLabel* installed_details;
	QLabel* catalog_detail_icon;
	QLabel* installed_detail_icon;
	QToolButton* catalog_details_toggle;
	QToolButton* installed_details_toggle;
	QFrame* catalog_details_separator;
	QFrame* installed_details_separator;
	QLabel* catalog_technical_details;
	QLabel* installed_technical_details;
	QPushButton* catalog_empty_action;
	QPushButton* installed_empty_action;
	QPushButton* catalog_install_button;
	QPushButton* remove_button;
	QLabel* installation_description;
	QLabel* installation_status;
	QTextBrowser* instructions_browser;
	QList<PluginPackageInfo> catalog_entries;
	QString selected_plugin_id;
	QSet<QString> pending_removals;
	QSet<QString> pending_enable_confirmations;
#if defined(METASIBERIA_PLUGIN_TESTING)
	bool auto_accept_install_confirmation_for_test = false;
	bool install_confirmation_requested_for_test = false;
#endif
};
