/*=====================================================================
TreeEditorPanel.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "TreeParams.h"
#include <QtWidgets/QWidget>


class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSettings;
class QSpinBox;
class WorldObject;


class TreeEditorPanel : public QWidget
{
	Q_OBJECT
public:
	explicit TreeEditorPanel(QWidget* parent = 0);

	void init(QSettings* settings);
	void setFromObject(const WorldObject& ob, bool ob_in_editing_users_world);
	void toObject(WorldObject& ob_out);
	void setControlsEnabled(bool enabled);
	void setControlsEditable(bool editable);
	void setTransformFromObject(const WorldObject& ob);
	void objectPickedUp();
	void objectDropped();
	bool posAndRot3DControlsEnabled() const;
	bool snapToGridChecked() const;
	double gridSpacing() const;

signals:
	void objectChanged();
	void objectTransformChanged();
	void posAndRot3DControlsToggled();
	void deleteObjectRequested();

private slots:
	void controlChanged();
	void rebuildNow();
	void randomSeed();
	void presetChanged(int index);
	void resetPreset();

private:
	void createUi();
	TreeParams controlsToParams() const;
	void setControlsFromParams(const TreeParams& params);
	void emitObjectChangedDebounced();
	QDoubleSpinBox* addDoubleSpin(QFormLayout* form, const QString& label, double min_v, double max_v, double step, int decimals);
	QSpinBox* addIntSpin(QFormLayout* form, const QString& label, int min_v, int max_v);

	QSettings* settings;
	bool updating;
	TreeParams current_params;
	QTimer* rebuild_timer;
	QLabel* info_label;

	QComboBox* preset_combo;
	QSpinBox* seed_spin;
	QDoubleSpinBox* height_spin;
	QDoubleSpinBox* scale_spin;
	QDoubleSpinBox* trunk_height_spin;
	QDoubleSpinBox* trunk_radius_spin;
	QDoubleSpinBox* trunk_taper_spin;
	QDoubleSpinBox* trunk_curve_spin;
	QDoubleSpinBox* trunk_twist_spin;
	QSpinBox* trunk_segments_spin;
	QSpinBox* trunk_sections_spin;
	QSpinBox* branch_levels_spin;
	QSpinBox* branches_per_level_spin;
	QDoubleSpinBox* branch_angle_spin;
	QDoubleSpinBox* branch_length_spin;
	QDoubleSpinBox* branch_radius_spin;
	QDoubleSpinBox* branch_randomness_spin;
	QDoubleSpinBox* branch_start_height_spin;
	QComboBox* leaf_type_combo;
	QSpinBox* leaf_count_spin;
	QDoubleSpinBox* leaf_size_spin;
	QDoubleSpinBox* leaf_alpha_spin;
	QComboBox* billboard_combo;
	QComboBox* quality_combo;
	QCheckBox* lod_checkbox;
	QComboBox* collision_combo;
	QCheckBox* cast_shadows_checkbox;
	QPushButton* regenerate_button;
	QPushButton* random_seed_button;
	QPushButton* reset_preset_button;
	QPushButton* delete_button;
};
