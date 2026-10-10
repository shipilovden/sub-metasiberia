/*=====================================================================
WorldSettingsWidget.h
---------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "../shared/WorldSettings.h"
#include "TerrainSpecSectionWidget.h"
#include "ui_WorldSettingsWidget.h"
#include <QtCore/QThread>
#include <QtCore/QByteArray>
#include <QtCore/QMutex>
#include <QtCore/QPointF>
#include <QtCore/QVector>
#include <vector>


class QSettings;
class MainWindow;
class QCheckBox;
class QComboBox;
class QSlider;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QToolButton;
class QWidget;
class RealTerrainMapWidget;


class RealTerrainImportThread : public QThread
{
	Q_OBJECT
public:
	RealTerrainImportThread(double north, double south, double west, double east, const QVector<QPointF>& polygon, float water_z, QObject* parent = NULL);
	~RealTerrainImportThread();
	void cancelImport();

	signals:
	void tileProgress(int completed, int total);
	void statusMessage(const QString& message);
	void importReady(const QByteArray& metres, const QByteArray& material_mask_png, const QByteArray& tree_mask_png,
		const QByteArray& road_mask_png, const QByteArray& building_mask_png, const QString& feature_status);
	void importFailed(const QString& message);

protected:
	void run() override;

private:
	double north;
	double south;
	double west;
	double east;
	QVector<QPointF> polygon;
	float water_z;
	QMutex client_mutex;
	class HTTPClient* active_client;
};


class WorldSettingsWidget : public QWidget, public Ui::WorldSettingsWidget
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	WorldSettingsWidget(QWidget* parent = NULL);
	~WorldSettingsWidget();

	void init(MainWindow* main_window);

	void retranslateUiText();

	void setFromWorldSettings(const WorldSettings& world_settings);

	void toWorldSettings(WorldSettings& world_settings_out);

	// Applies the albedo texture from a material selected in the material browser
	// to the terrain layer whose "Choose material" button was used.
	bool setTerrainMaterialFromBrowser(const std::string& material_path);

	void updateControlsEditable();

signals:
	void settingsChangedSignal();
	void sculptingModeChangedSignal(bool enabled);
	void sculptingToolChangedSignal(int tool);
	void sculptingBrushSettingsChangedSignal(float radius_m, float strength_m, float target_height_m);
	void sculptingIslandSettingsChangedSignal(float sea_floor_m, float land_base_m, float peak_m, int seed);
	void sculptingIslandRegenerateRequestedSignal();
	void sculptingIslandPlacementRequestedSignal();
	void sculptingMapPaintTargetChangedSignal(int target);
	void sculptingUndoSignal();
	void sculptingRedoSignal();

protected slots:
	void newTerrainSectionPushButtonClicked();

	void removeTerrainSectionButtonClickedSlot();

	void applySettingsSlot();

	void settingsChangedSlot();
	void waterSurfEnabledToggledSlot(bool enabled);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void setTerrainSectionAreaHeight(int target_height);
	bool shouldStartTerrainSectionResize(const QPoint& pos_in_scroll_area) const;

	URLString getURLForFileSelectWidget(FileSelectWidget* widget);
	void createSculptingTab();
	void setSculptingControlsEnabled(bool enabled);
	void retranslateSculptingTab();
	void applyRecommendedWaterSurfSettings();
	void updateTerrainMaterialPreview(int index);
	void browsePolyHavenTerrainTexture(int index);

	//std::vector<TerrainSpecSectionWidget*> section_widgets;
	MainWindow* main_window;
	bool terrain_section_resize_drag_active;
	int terrain_section_resize_drag_start_global_y;
	int terrain_section_resize_drag_start_height;
	int terrain_material_browser_target;
	QLabel* terrain_material_preview_labels[4];

	QTabWidget* settings_tabs;
	QWidget* sculpting_tab;
	QToolButton* sculpting_basic_accordion_button;
	QWidget* sculpting_basic_panel;
	QToolButton* sculpting_shape_accordion_button;
	QWidget* sculpting_shape_panel;
	QToolButton* sculpting_stamp_accordion_button;
	QWidget* sculpting_stamp_panel;
	QCheckBox* sculpting_mode_check_box;
	QComboBox* sculpting_map_target_combo;
	QLabel* sculpting_map_target_label;
	QToolButton* sculpting_layers_accordion_button;
	QGroupBox* sculpting_layers_group;
	QListWidget* sculpting_layers_list;
	QDoubleSpinBox* sculpting_radius_spin_box;
	QDoubleSpinBox* sculpting_strength_spin_box;
	QDoubleSpinBox* sculpting_target_height_spin_box;
	QGroupBox* sculpting_island_settings_group;
	QLabel* sculpting_island_sea_floor_label;
	QLabel* sculpting_island_land_base_label;
	QLabel* sculpting_island_peak_label;
	QLabel* sculpting_island_seed_label;
	QSlider* sculpting_island_sea_floor_slider;
	QSlider* sculpting_island_land_base_slider;
	QSlider* sculpting_island_peak_slider;
	QSlider* sculpting_island_seed_slider;
	QLabel* sculpting_island_sea_floor_value;
	QLabel* sculpting_island_land_base_value;
	QLabel* sculpting_island_peak_value;
	QLabel* sculpting_island_seed_value;
	QLabel* sculpting_island_preview_label;
	QLabel* sculpting_island_preview_image;
	QPushButton* sculpting_island_random_seed_button;
	QPushButton* sculpting_island_regenerate_button;
	QPushButton* sculpting_island_place_button;
	QLabel* sculpting_radius_label;
	QLabel* sculpting_strength_label;
	QLabel* sculpting_target_height_label;
	QPushButton* sculpting_tool_buttons[17];
	QPushButton* sculpting_shape_tool_buttons[21];
	QPushButton* sculpting_stamp_tool_buttons[16];
	QPushButton* sculpting_undo_button;
	QPushButton* sculpting_redo_button;
	QLabel* sculpting_status_label;
	QGroupBox* real_terrain_group;
	QLabel* real_terrain_map_label;
	QComboBox* real_terrain_basemap_combo;
	QLabel* real_terrain_section_x_label;
	QLabel* real_terrain_section_y_label;
	QLabel* real_terrain_source_label;
	QLabel* real_terrain_attribution_label;
	RealTerrainMapWidget* real_terrain_map_widget;
	QPushButton* real_terrain_rectangle_button;
	QPushButton* real_terrain_freehand_button;
	QPushButton* real_terrain_clear_button;
	QPushButton* real_terrain_zoom_out_button;
	QPushButton* real_terrain_zoom_in_button;
	QSpinBox* real_terrain_section_x_spin_box;
	QSpinBox* real_terrain_section_y_spin_box;
	QPushButton* real_terrain_import_button;
	QLabel* real_terrain_status_label;
	RealTerrainImportThread* real_terrain_import_thread;
};
