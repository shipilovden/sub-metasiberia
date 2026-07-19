/*=====================================================================
ScientificObjectEditor.h
------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "ScientificObjectSettings.h"
#include "../shared/WorldObject.h"
#include <QtCore/QPoint>
#include <QtGui/QPixmap>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>


class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QGroupBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QPlainTextEdit;
class QSpinBox;
class QTabWidget;
class QTextEdit;
class QTimer;
class QSettings;
class MoleculeViewportWidget;
class ScientificImageViewer;


class ScientificObjectEditor : public QWidget
{
	Q_OBJECT

public:
	explicit ScientificObjectEditor(QWidget* parent = 0);
	~ScientificObjectEditor();

	void init(QSettings* settings_);
	void setFromObject(const WorldObject& ob, bool ob_in_editing_users_world);
	void setTransformFromObject(const WorldObject& ob);
	void toObject(WorldObject& ob_out);
	void writeTransformMembersToObject(WorldObject& ob_out);
	void objectLastModifiedUpdated(const WorldObject& ob);
	void objectPickedUp();
	void objectDropped();
	void setControlsEnabled(bool enabled);
	void setControlsEditable(bool editable);
	bool posAndRot3DControlsEnabled() const;
	bool snapToGridChecked() const;
	double gridSpacing() const;
	bool handleSceneRay(const Vec4f& ray_origin_os, const Vec4f& ray_dir_os, bool show_context_menu, bool additive, const QPoint& global_pos);

	static int runPubChemSmokeCheck(const QString& report_path);
	static int runPubChemApplySmokeCheck(const QString& report_path);
	static int runMoleculeInformationSmokeCheck(const QString& report_path);

signals:
	void objectTransformChanged();
	void objectChanged();
	void posAndRot3DControlsToggled();
	void deleteObjectRequested();

private slots:
	void emitObjectChanged();
	void emitTransformChanged();
	void browseFile();
	void sourceModeChanged(int index);
	void onlineDatabaseChanged(int index);
	void aiProviderChanged(int index);
	void saveAiApiKey();
	void generateCodeFromPrompt();
	void loadScientificSourceResult();
	void previewScientificSourceResult();
	void updateScientificSourceUiState();
	void updateColourButton();
	void updateMoleculeInteractiveView();
	void moleculeCardSectionChanged(int index);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	QGroupBox* createSection(const QString& title, QWidget* parent, QGridLayout** grid_out);
	QLineEdit* addLineEdit(QGridLayout* grid, int& row, const QString& label, const QString& placeholder = QString());
	QPlainTextEdit* addPlainTextEdit(QGridLayout* grid, int& row, const QString& label, int min_height = 74);
	QComboBox* addComboBox(QGridLayout* grid, int& row, const QString& label);
	QDoubleSpinBox* addDoubleSpinBox(QGridLayout* grid, int& row, const QString& label, double min_v, double max_v, double step, int decimals = 3);
	QSpinBox* addSpinBox(QGridLayout* grid, int& row, const QString& label, int min_v, int max_v);
	QCheckBox* addCheckBox(QGridLayout* grid, int& row, const QString& label);
	void installTooltips();
	void connectObjectChangeSignals();
	void setControlsFromSettings(const ScientificObjectSettings& settings);
	ScientificObjectSettings controlsToSettings() const;
	void applyScientificMaterial(WorldObject& ob_out, const ScientificObjectSettings& settings);
	void applyScientificPhysics(WorldObject& ob_out, const ScientificObjectSettings& settings);
	void updateInfoLabel(const WorldObject& ob);
	void setScientificSourceResult(const QString& database, const QString& query, bool load);
	bool searchPubChem(const QString& query);
	bool loadPubChemCID(const QString& cid);
	bool loadPubChemCardSection(const QString& section_key);
	bool loadPubChemImage();
	void handleMoleculeAction(const QString& action, int index);
	QString propertyValue(const QString& key) const;
	QString generatedPythonForPrompt(const QString& prompt) const;
	QString currentComboData(const QComboBox* combo, const QString& fallback) const;
	void setComboData(QComboBox* combo, const QString& item_data);
	QString aiSettingsProviderKey() const;
	void populateStaticCombos();
	QString selectionModeDisplayText(const QString& mode) const;
	QString selectionStateDisplayText(const QString& state) const;
	void updateMoleculeSelectionStatus();
	void updateMoleculeImagePreview();

	QSettings* settings;
	UID editing_ob_uid;
	bool controls_editable;
	bool syncing;
	ScientificObjectSettings current_settings;

	QLabel* info_label;
	QTabWidget* tab_widget;

	QCheckBox* show_3d_controls_checkbox;
	QCheckBox* snap_to_grid_checkbox;
	QDoubleSpinBox* grid_spacing_spin;
	QDoubleSpinBox* pos_x_spin;
	QDoubleSpinBox* pos_y_spin;
	QDoubleSpinBox* pos_z_spin;
	QDoubleSpinBox* scale_x_spin;
	QDoubleSpinBox* scale_y_spin;
	QDoubleSpinBox* scale_z_spin;
	QDoubleSpinBox* rot_x_spin;
	QDoubleSpinBox* rot_y_spin;
	QDoubleSpinBox* rot_z_spin;

	QLineEdit* name_edit;
	QComboBox* type_combo;
	QPlainTextEdit* description_edit;
	QLineEdit* source_edit;
	QLineEdit* author_edit;
	QLineEdit* tags_edit;
	QLineEdit* uuid_edit;
	QLineEdit* created_edit;
	QLineEdit* modified_edit;

	QComboBox* source_mode_combo;
	QLineEdit* file_path_edit;
	QPushButton* browse_file_button;
	QLineEdit* url_edit;
	QComboBox* online_database_combo;
	QLineEdit* online_query_edit;
	QPushButton* online_search_button;
	QListWidget* online_results_list;
	QPushButton* online_preview_button;
	QPushButton* online_load_button;
	QLabel* source_status_label;
	QLabel* molecule_image_label;
	QPixmap molecule_image_preview_pixmap;
	double molecule_image_preview_zoom;
	QLabel* query_resolution_label;
	QComboBox* code_language_combo;
	QPlainTextEdit* code_edit;
	QPlainTextEdit* prompt_edit;

	QPlainTextEdit* data_summary_edit;
	QPlainTextEdit* atom_table_edit;
	QPlainTextEdit* bond_table_edit;
	QPlainTextEdit* point_table_edit;
	QPlainTextEdit* value_table_edit;
	QPlainTextEdit* property_table_edit;

	QComboBox* visualization_mode_combo;
	QComboBox* colour_scheme_combo;
	QPushButton* display_colour_button;
	QComboBox* material_combo;
	QDoubleSpinBox* atom_radius_spin;
	QDoubleSpinBox* bond_thickness_spin;
	QDoubleSpinBox* point_size_spin;
	QDoubleSpinBox* line_width_spin;
	QDoubleSpinBox* opacity_spin;
	QDoubleSpinBox* object_scale_spin;
	QCheckBox* show_labels_check;
	QCheckBox* atom_labels_pinned_check;
	QCheckBox* show_molecule_title_check;
	QLineEdit* molecule_title_edit;
	QCheckBox* molecule_title_pinned_check;
	QCheckBox* show_info_card_check;
	QComboBox* info_card_mode_combo;
	QDoubleSpinBox* info_card_scale_spin;
	QDoubleSpinBox* info_card_distance_spin;
	QCheckBox* info_card_pinned_check;
	QCheckBox* info_card_dark_background_check;
	QComboBox* info_card_stand_type_combo;
	QCheckBox* info_card_auto_fit_text_check;
	QDoubleSpinBox* info_card_stand_width_spin;
	QDoubleSpinBox* info_card_stand_height_spin;
	QDoubleSpinBox* info_card_stand_depth_spin;
	QCheckBox* show_legend_check;
	QCheckBox* show_hydrogen_check;
	QComboBox* label_mode_combo;
	QPushButton* label_colour_button;
	QDoubleSpinBox* label_scale_spin;
	QDoubleSpinBox* molecule_title_scale_spin;
	QSpinBox* label_max_count_spin;
	QDoubleSpinBox* label_max_distance_spin;
	QSpinBox* lod_spin;
	QCheckBox* glow_enabled_check;
	QDoubleSpinBox* glow_strength_spin;
	QCheckBox* outline_enabled_check;
	QCheckBox* wireframe_enabled_check;
	QComboBox* selection_mode_combo;
	MoleculeViewportWidget* molecule_viewport;
	QLabel* molecule_selection_status_label;

	QCheckBox* collision_enabled_check;
	QCheckBox* solid_check;
	QCheckBox* trigger_check;
	QCheckBox* selectable_check;
	QCheckBox* movable_check;
	QCheckBox* gravity_enabled_check;
	QComboBox* physics_motion_type_combo;
	QComboBox* physics_shape_combo;
	QLineEdit* collision_layer_edit;
	QDoubleSpinBox* physics_mass_spin;
	QDoubleSpinBox* physics_friction_spin;
	QDoubleSpinBox* physics_restitution_spin;

	QCheckBox* measure_distance_check;
	QCheckBox* measure_angle_check;
	QCheckBox* measure_torsion_check;
	QCheckBox* measure_area_check;
	QCheckBox* measure_volume_check;
	QSpinBox* atom_count_spin;
	QSpinBox* bond_count_spin;
	QSpinBox* point_count_spin;
	QLineEdit* dimensions_edit;
	QPlainTextEdit* measurement_records_edit;
	QLabel* molecule_metrics_label;
	QPushButton* start_distance_button;
	QPushButton* start_angle_button;
	QPushButton* start_torsion_button;
	QPushButton* clear_measurements_button;

	QCheckBox* rotation_animation_check;
	QCheckBox* trajectory_animation_check;
	QCheckBox* vibration_animation_check;
	QCheckBox* time_series_check;
	QDoubleSpinBox* animation_speed_spin;
	QComboBox* animation_direction_combo;
	QSpinBox* current_frame_spin;
	QSpinBox* frame_count_spin;

	QCheckBox* simulation_enabled_check;
	QComboBox* simulation_type_combo;
	QPlainTextEdit* simulation_notes_edit;
	QLabel* simulation_status_label;
	QLabel* animation_status_label;
	QTimer* molecule_world_spin_timer;

	QWidget* molecule_card_tab;
	QTabWidget* molecule_card_sections;
	QVector<QPlainTextEdit*> molecule_card_edits;
	QVector<QLabel*> molecule_card_status_labels;
	ScientificImageViewer* image_viewer;
	QPlainTextEdit* provider_classification_edit;
	QPlainTextEdit* computed_classification_edit;
	QLineEdit* user_collections_edit;
	QCheckBox* favorite_check;
	QLabel* catalog_status_label;

	QPlainTextEdit* ai_prompt_edit;
	QPlainTextEdit* generated_code_edit;
	QComboBox* ai_provider_combo;
	QLineEdit* ai_model_edit;
	QLineEdit* ai_endpoint_edit;
	QLineEdit* ai_api_key_edit;
	QCheckBox* ai_user_credentials_check;
	QPushButton* ai_generate_code_button;
	QPushButton* ai_create_object_button;
	QPushButton* ai_explain_button;
	QPushButton* ai_optimise_button;

	QPlainTextEdit* custom_properties_edit;
	Colour3f display_colour;
};
