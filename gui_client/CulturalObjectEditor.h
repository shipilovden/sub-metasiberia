/*=====================================================================
CulturalObjectEditor.h
----------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "CulturalApiClient.h"
#include "CulturalObjectSettings.h"
#include "../shared/WorldObject.h"
#include <QtCore/QHash>
#include <QtGui/QPixmap>
#include <QtWidgets/QWidget>
#include <map>
#include <vector>


class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;


class CulturalObjectEditor : public QWidget
{
	Q_OBJECT

public:
	explicit CulturalObjectEditor(QWidget* parent = 0);
	static CulturalObjectSettings settingsFromProviderRecord(const CulturalApiRecord& record);

	void setFromObject(const WorldObject& ob, bool ob_in_editing_users_world);
	void setTransformFromObject(const WorldObject& ob);
	void toObject(WorldObject& ob_out);
	void writeTransformMembersToObject(WorldObject& ob_out);
	void objectLastModifiedUpdated(const WorldObject& ob);
	void objectPickedUp();
	void objectDropped();
	void setControlsEnabled(bool enabled);
	void setControlsEditable(bool editable);
	void setCachedPrimaryImageURL(const QString& resource_url, const QString& source_url, const QString& error_message = QString());
	bool posAndRot3DControlsEnabled() const;
	void setPosAndRot3DControlsEnabled(bool enabled);
	bool snapToGridChecked() const;
	double gridSpacing() const;

signals:
	void objectTransformChanged();
	void objectChanged();
	void publicImageImportRequested(const QString& source_url);
	void addCulturalObjectRequested(const QString& provider_id, const QString& record_id);
	void posAndRot3DControlsToggled();
	void deleteObjectRequested();

private:
	QLineEdit* addLine(QGridLayout* grid, int& row, const char* key, const QString& label, const QString& placeholder = QString());
	QPlainTextEdit* addText(QGridLayout* grid, int& row, const char* key, const QString& label, int height = 74);
	QComboBox* addCombo(QGridLayout* grid, int& row, const char* key, const QString& label, const QStringList& values);
	QCheckBox* addCheck(QGridLayout* grid, int& row, const char* key, const QString& label);
	QDoubleSpinBox* addDouble(QGridLayout* grid, int& row, const char* key, const QString& label, double min, double max, double step, int decimals = 3);
	QWidget* makeTab(const QString& title, QGridLayout** grid_out);
	void connectChangeSignals();
	void setControlsFromSettings(const CulturalObjectSettings& settings);
	CulturalObjectSettings controlsToSettings() const;
	QString line(const char* key) const;
	QString text(const char* key) const;
	QString combo(const char* key) const;
	bool checked(const char* key) const;
	double number(const char* key) const;
	void setStatus(const QString& status, bool error = false);
	void importLocalJson();
	void searchOnline();
	void importProviderRecord(const CulturalApiRecord& record);
	void showRawJson();

	struct CatalogueControls
	{
		CatalogueControls() : page_widget(NULL), query(NULL), artist(NULL), style(NULL), theme(NULL), region(NULL), material(NULL), period(NULL), department(NULL), object_type(NULL), artist_selector(NULL), style_selector(NULL), theme_selector(NULL), region_selector(NULL), material_selector(NULL), period_selector(NULL), department_selector(NULL), object_type_selector(NULL), date_begin(NULL), date_end(NULL), public_domain(NULL), has_images(NULL), audio(NULL), video(NULL), title_only(NULL), artist_or_culture(NULL), on_view(NULL), highlights(NULL), result_list(NULL), preview(NULL), details(NULL), status(NULL), previous_page(NULL), next_page(NULL), page(1), has_searched(false), preview_generation(0) {}

		QString provider_id;
		QWidget* page_widget;
		QLineEdit* query;
		QLineEdit* artist;
		QLineEdit* style;
		QLineEdit* theme;
		QLineEdit* region;
		QLineEdit* material;
		QLineEdit* period;
		QLineEdit* department;
		QLineEdit* object_type;
		// ArtIC uses curated drop-down lists.  The text fields above are retained
		// for The Met, whose API is query-driven rather than facet-driven.
		QComboBox* artist_selector;
		QComboBox* style_selector;
		QComboBox* theme_selector;
		QComboBox* region_selector;
		QComboBox* material_selector;
		QComboBox* period_selector;
		QComboBox* department_selector;
		QComboBox* object_type_selector;
		QSpinBox* date_begin;
		QSpinBox* date_end;
		QCheckBox* public_domain;
		QCheckBox* has_images;
		QCheckBox* audio;
		QCheckBox* video;
		QCheckBox* title_only;
		QCheckBox* artist_or_culture;
		QCheckBox* on_view;
		QCheckBox* highlights;
		QListWidget* result_list;
		QLabel* preview;
		QPlainTextEdit* details;
		QLabel* status;
		QPushButton* previous_page;
		QPushButton* next_page;
		int page;
		bool has_searched;
		quint64 preview_generation;
		QHash<QString, QPixmap> preview_cache;
		CulturalApiSearchResult result;
	};

	QWidget* makeCatalogueTab(CatalogueControls& catalogue, const QString& title, bool is_artic);
	CulturalApiSearchOptions catalogueOptions(const CatalogueControls& catalogue) const;
	void searchCatalogue(CatalogueControls& catalogue, bool reset_page);
	void queueCataloguePreviewFetch(CatalogueControls& catalogue);
	void fetchCataloguePreview(CatalogueControls* catalogue, quint64 generation, int index);
	void updateCatalogueDetails(CatalogueControls& catalogue);
	void importSelectedCatalogueRecord(CatalogueControls& catalogue);
	void showSelectedCatalogueJson(CatalogueControls& catalogue);
	void addSelectedCatalogueRecord(CatalogueControls& catalogue);

	UID editing_ob_uid;
	bool controls_editable;
	bool syncing;
	CulturalObjectSettings current_settings;
	QLabel* info_label;
	QLabel* status_label;
	QTabWidget* tabs;
	CatalogueControls artic_catalogue;
	CatalogueControls met_catalogue;
	std::map<std::string, QLineEdit*> lines;
	std::map<std::string, QPlainTextEdit*> texts;
	std::map<std::string, QComboBox*> combos;
	std::map<std::string, QCheckBox*> checks;
	std::map<std::string, QDoubleSpinBox*> doubles;

	QCheckBox* show_3d_controls;
	QCheckBox* link_scale;
	QCheckBox* snap_to_grid;
	QDoubleSpinBox* grid_spacing;
	QDoubleSpinBox* pos_x;
	QDoubleSpinBox* pos_y;
	QDoubleSpinBox* pos_z;
	QDoubleSpinBox* scale_x;
	QDoubleSpinBox* scale_y;
	QDoubleSpinBox* scale_z;
	QDoubleSpinBox* rot_x;
	QDoubleSpinBox* rot_y;
	QDoubleSpinBox* rot_z;
};
