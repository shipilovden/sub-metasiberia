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

	//std::vector<TerrainSpecSectionWidget*> section_widgets;
	MainWindow* main_window;
	bool terrain_section_resize_drag_active;
	int terrain_section_resize_drag_start_global_y;
	int terrain_section_resize_drag_start_height;
};
