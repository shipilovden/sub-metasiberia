/*=====================================================================
WorldSettingsWidget.h
---------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "../shared/WorldSettings.h"
#include "TerrainSpecSectionWidget.h"
#include "ui_WorldSettingsWidget.h"
#include <vector>


class QSettings;
class MainWindow;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QToolButton;
class QWidget;


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

	void updateControlsEditable();

signals:
	void settingsChangedSignal();
	void sculptingModeChangedSignal(bool enabled);
	void sculptingToolChangedSignal(int tool);
	void sculptingBrushSettingsChangedSignal(float radius_m, float strength_m);
	void sculptingUndoSignal();
	void sculptingRedoSignal();

protected slots:
	void newTerrainSectionPushButtonClicked();

	void removeTerrainSectionButtonClickedSlot();

	void applySettingsSlot();

	void settingsChangedSlot();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void setTerrainSectionAreaHeight(int target_height);
	bool shouldStartTerrainSectionResize(const QPoint& pos_in_scroll_area) const;

	URLString getURLForFileSelectWidget(FileSelectWidget* widget);
	void createSculptingTab();
	void setSculptingControlsEnabled(bool enabled);
	void retranslateSculptingTab();

	//std::vector<TerrainSpecSectionWidget*> section_widgets;
	MainWindow* main_window;
	bool terrain_section_resize_drag_active;
	int terrain_section_resize_drag_start_global_y;
	int terrain_section_resize_drag_start_height;

	QTabWidget* settings_tabs;
	QWidget* sculpting_tab;
	QToolButton* sculpting_basic_accordion_button;
	QWidget* sculpting_basic_panel;
	QCheckBox* sculpting_mode_check_box;
	QDoubleSpinBox* sculpting_radius_spin_box;
	QDoubleSpinBox* sculpting_strength_spin_box;
	QPushButton* sculpting_tool_buttons[4];
	QPushButton* sculpting_undo_button;
	QPushButton* sculpting_redo_button;
	QLabel* sculpting_status_label;
};
