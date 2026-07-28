/*=====================================================================
TreeEditorPanel.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "TreeParams.h"
#include <QtWidgets/QWidget>
#include <array>
#include <string>


class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QHideEvent;
class QLabel;
class QPushButton;
class QSettings;
class QSpinBox;
class QShowEvent;
class QTabWidget;
class WorldObject;


class TreeEditorPanel : public QWidget
{
	Q_OBJECT
public:
	explicit TreeEditorPanel(QWidget* parent = 0);
	static int runSmokeCheck(const std::string& report_path);

	void init(QSettings* settings, const std::string& asset_root_path);
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

protected:
	void hideEvent(QHideEvent* event) override;
	void showEvent(QShowEvent* event) override;

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
	std::string asset_root_path;
	bool updating;
	bool controls_editable;
	bool pending_mesh_rebuild;
	bool preserve_current_params_for_next_to_object;
	TreeParams current_params;
	QTimer* rebuild_timer;
	QTabWidget* mode_tabs;
	QLabel* info_label;

	QComboBox* preset_combo;
	QComboBox* tree_type_combo;
	QSpinBox* seed_spin;
	QDoubleSpinBox* height_spin;
	QDoubleSpinBox* trunk_height_spin;
	QDoubleSpinBox* trunk_radius_spin;
	QDoubleSpinBox* trunk_taper_spin;
	QDoubleSpinBox* trunk_curve_spin;
	QDoubleSpinBox* trunk_twist_spin;
	QSpinBox* trunk_segments_spin;
	QSpinBox* trunk_sections_spin;
	QDoubleSpinBox* bark_red_spin;
	QDoubleSpinBox* bark_green_spin;
	QDoubleSpinBox* bark_blue_spin;
	QComboBox* bark_texture_combo;
	QCheckBox* bark_textured_checkbox;
	QCheckBox* bark_flat_shading_checkbox;
	QDoubleSpinBox* bark_texture_scale_x_spin;
	QDoubleSpinBox* bark_texture_scale_y_spin;
	QSpinBox* branch_levels_spin;
	QSpinBox* branches_per_level_spin;
	QDoubleSpinBox* branch_angle_spin;
	QDoubleSpinBox* branch_length_spin;
	QDoubleSpinBox* branch_radius_spin;
	QDoubleSpinBox* branch_taper_spin;
	QDoubleSpinBox* branch_curve_spin;
	QDoubleSpinBox* branch_twist_spin;
	QDoubleSpinBox* branch_randomness_spin;
	QDoubleSpinBox* branch_start_height_spin;
	QDoubleSpinBox* branch_force_x_spin;
	QDoubleSpinBox* branch_force_y_spin;
	QDoubleSpinBox* branch_force_z_spin;
	QDoubleSpinBox* branch_force_strength_spin;
	QDoubleSpinBox* branch_gnarliness_spin;
	std::array<QDoubleSpinBox*, 4> branch_angle_level_spins;
	std::array<QSpinBox*, 4> branch_children_level_spins;
	std::array<QDoubleSpinBox*, 4> branch_length_level_spins;
	std::array<QDoubleSpinBox*, 4> branch_radius_level_spins;
	std::array<QSpinBox*, 4> branch_sections_level_spins;
	std::array<QSpinBox*, 4> branch_segments_level_spins;
	std::array<QDoubleSpinBox*, 4> branch_start_level_spins;
	std::array<QDoubleSpinBox*, 4> branch_taper_level_spins;
	std::array<QDoubleSpinBox*, 4> branch_twist_level_spins;
	std::array<QDoubleSpinBox*, 4> branch_gnarliness_level_spins;
	QComboBox* leaf_type_combo;
	QSpinBox* leaf_count_spin;
	QDoubleSpinBox* leaf_angle_spin;
	QDoubleSpinBox* leaf_size_spin;
	QDoubleSpinBox* leaf_size_randomness_spin;
	QDoubleSpinBox* leaf_red_spin;
	QDoubleSpinBox* leaf_green_spin;
	QDoubleSpinBox* leaf_blue_spin;
	QDoubleSpinBox* leaf_alpha_spin;
	QDoubleSpinBox* leaf_alpha_test_spin;
	QCheckBox* leaf_rounded_normals_checkbox;
	QDoubleSpinBox* leaf_start_spin;
	QComboBox* billboard_combo;
	QCheckBox* trellis_enabled_checkbox;
	QDoubleSpinBox* trellis_x_spin;
	QDoubleSpinBox* trellis_y_spin;
	QDoubleSpinBox* trellis_z_spin;
	QDoubleSpinBox* trellis_width_spin;
	QDoubleSpinBox* trellis_height_spin;
	QDoubleSpinBox* trellis_spacing_spin;
	QDoubleSpinBox* trellis_force_strength_spin;
	QDoubleSpinBox* trellis_force_max_distance_spin;
	QDoubleSpinBox* trellis_force_falloff_spin;
	QDoubleSpinBox* trellis_cylinder_radius_spin;
	QCheckBox* trellis_visible_checkbox;
	QComboBox* quality_combo;
	QCheckBox* lod_checkbox;
	QComboBox* collision_combo;
	QCheckBox* cast_shadows_checkbox;
	QPushButton* regenerate_button;
	QPushButton* random_seed_button;
	QPushButton* reset_preset_button;
	QPushButton* delete_button;
};
