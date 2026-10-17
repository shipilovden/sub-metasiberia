/*=====================================================================
TerrainSpecSectionWidget.h
--------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "ui_TerrainSpecSectionWidget.h"


class QSettings;
class QPushButton;
class QLabel;
class QDoubleSpinBox;


class TerrainSpecSectionWidget : public QWidget, public Ui::TerrainSpecSectionWidget
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	TerrainSpecSectionWidget(QWidget* parent);
	~TerrainSpecSectionWidget();

	void updateControlsEditable(bool editable);
	void retranslateMapClearButtons();
	void clearHeightMap();
	void setMapPreviewPath(int index, const QString& path);
	QDoubleSpinBox* mapScaleSpinBox(int index, bool y_axis = false) const;
signals:
	void removeButtonClickedSignal();
	void mapClearedSignal();
private:
	QPushButton* map_clear_buttons[5];
	QLabel* map_preview_labels[5];
	QLabel* map_scale_labels[5];
	QLabel* map_scale_y_labels[5];
	QDoubleSpinBox* map_scale_spin_boxes[5];
	QDoubleSpinBox* map_scale_y_spin_boxes[5];
};
