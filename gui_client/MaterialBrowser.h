/*=====================================================================
MaterialBrowser.h
--------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "ui_MaterialBrowser.h"
#include <utils/Reference.h>
#include <string>
#include <vector>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QSet>
#include <QtGui/QIcon>
#include <atomic>
#include <memory>

class QPushButton;
class FlowLayout;
class OpenGLEngine;
class PrintOutput;
class QTabWidget;
class QTreeWidget;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QLabel;
class QComboBox;


/*=====================================================================
MaterialBrowser
---------------
Displays a bunch of buttons with material preview images on them.
=====================================================================*/
class MaterialBrowser : public QWidget, public Ui_MaterialBrowser
{
	Q_OBJECT
public:
	MaterialBrowser();
	~MaterialBrowser();

	void init(QWidget* parent, const std::string& basedir_path, const std::string& appdata_path, PrintOutput* print_output);


	void renderThumbnails(Reference<OpenGLEngine> opengl_engine);

signals:;
	void materialSelected(const std::string& path);
	void polyHavenMaterialRequested();
	void polyHavenMaterialReady(const std::string& path);

private slots:;
	void buttonClicked();

private:
	void loadPolyHavenCatalog();
	void rebuildCategories();
	void filterPolyHavenMaterials();
	void loadCatalogThumbnails();
	void updateCatalogThumbnailStatus();
	void applyCatalogThumbnail(const QString& url, const QIcon& icon);
	void selectPolyHavenMaterial(QListWidgetItem* item);
	void startMaterialDownload();
	void retranslateBrowser();
	void changeEvent(QEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

	QTabWidget* source_tabs = nullptr;
	QTreeWidget* categories = nullptr;
	QListWidget* polyhaven_materials = nullptr;
	QLineEdit* search = nullptr;
	QComboBox* sort = nullptr;
	QLabel* status = nullptr;
	QLabel* attribution = nullptr;
	QPushButton* refresh = nullptr;
	QJsonObject catalog;
	QHash<QString, QIcon> thumbnail_icons;
	QSet<QString> pending_thumbnails;
	QHash<QString, int> thumbnail_attempts;
	QHash<QString, qint64> thumbnail_retry_after;
	int thumbnail_scan_offset = 0;
	QString pending_material_id;
	int selection_generation = 0;
	bool catalog_loading = false;
	bool material_loading = false;
	bool thumbnail_preload_started = false;
	std::shared_ptr<std::atomic<bool>> alive;

	FlowLayout *				flow_layout;
	std::vector<QPushButton *>	browser_buttons;
	std::vector<std::string>	mat_paths;

	std::string basedir_path, appdata_path;
	PrintOutput* print_output;
};
