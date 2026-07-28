/*=====================================================================
VoxelEditorPanel.h
------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "VoxelProceduralGenerator.h"
#include "VoxelTools.h"
#include <QtCore/QString>
#include <QtWidgets/QWidget>
#include <cstdint>
#include <string>
#include <vector>


class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QWidget;
class WorldObject;


// Qt panel for the first native Metasiberia voxel-editor stage.  Geometry
// editing is performed by GUIClient/VoxelTools; this widget owns editor state,
// material colours and the controls that select those operations.
class VoxelEditorPanel : public QWidget
{
	Q_OBJECT
public:
	explicit VoxelEditorPanel(QWidget* parent = 0);

	void setFromObject(const WorldObject& object);
	bool applyToObject(WorldObject& object, std::string& error_out);
	void setLegacyContent(const std::string& content);
	const std::string& legacyContent() const { return editor_state.legacy_content; }
	void setEditable(bool editable);
	void setIconDirectory(const QString& directory);

	int currentMaterialIndex() const;
	VoxelToolSettings toolSettings() const;
	VoxelProceduralParams proceduralParams(VoxelProceduralType type) const;
	Vec3<int> selectionOffset() const;
	VoxelToolType currentTool() const;
	bool sceneToolsEnabled() const;
	void setSceneToolsEnabled(bool enabled);

	// Called after a scene edit or undo/redo.  It deliberately does not reload
	// controls, so an in-progress tool configuration is not discarded.
	void notifyVoxelDataChanged(const WorldObject& object);
	void selectMaterialIndex(int material_index);
	bool handleShortcut(QKeyEvent* event);

	// Runs deterministic data/tool/undo and widget round-trip checks.  A short
	// JSON-like report is returned in report_out; no files or world state change.
	static bool runSmokeCheck(std::string& report_out);

signals:
	void objectMetadataChanged();
	void meshRebuildRequested();
	void toolStateChanged();
	void proceduralGenerationRequested(int type);
	void selectionCopyRequested();
	void selectionPasteRequested();
	void selectionDeleteRequested();
	void selectionDuplicateRequested();
	void selectionMoveRequested();
	void selectionClearRequested();

private slots:
	void toolButtonClicked(int id);
	void toolControlChanged();
	void chooseColour();
	void paletteColourClicked();
	void layerSelectionChanged(int row);
	void layerNameEdited();
	void layerVisibleChanged(bool checked);
	void layerLockedChanged(bool checked);
	void layerOpacityChanged(double value);
	void addLayer();
	void deleteLayer();
	void moveLayerUp();
	void moveLayerDown();
	void renderModeChanged(int index);
	void rebuildMesh();
	void proceduralControlChanged();
	void randomiseProceduralSeed();

private:
	void createUi();
	void selectTool(VoxelToolType tool, bool emit_change);
	void setCurrentColour(uint32_t rgba, bool emit_change);
	void refreshAllControls();
	void refreshToolControls();
	void refreshColourControls();
	void refreshPaletteGrid();
	void refreshLayerList();
	void refreshActiveLayerControls();
	void refreshRenderControls();
	void refreshProceduralMetrics();
	void applyIcons();
	void updateVoxelCount(size_t count);
	void rebuildMaterialColourCache(const WorldObject& object);
	int findMaterialForColourInActiveLayer(uint32_t rgba) const;
	bool activeLayerEditable() const;
	void emitMetadataChange();
	void emitMeshChange();

	VoxelEditorState editor_state;
	VoxelToolType selected_tool;
	std::vector<uint32_t> material_colours;
	std::vector<int> removed_material_indices;
	bool updating;
	bool editable;
	bool loaded_editor_metadata;
	bool editor_metadata_dirty;
	bool pending_colour_material;
	bool layer_render_dirty;
	QString icon_directory;

	QLabel* info_label;
	QLabel* voxel_count_label;
	QCheckBox* scene_tools_checkbox;
	QButtonGroup* tool_button_group;
	QWidget* tool_settings_widget;
	QSpinBox* brush_size_spin;
	QComboBox* brush_shape_combo;
	QComboBox* brush_mode_combo;
	QCheckBox* hollow_checkbox;
	QCheckBox* mirror_x_checkbox;
	QCheckBox* mirror_y_checkbox;
	QCheckBox* mirror_z_checkbox;

	QPushButton* current_colour_button;
	QWidget* palette_widget;
	QGridLayout* palette_grid;
	QLabel* recent_label;
	QGridLayout* recent_grid;

	QListWidget* layer_list;
	QPushButton* add_layer_button;
	QPushButton* delete_layer_button;
	QPushButton* move_layer_up_button;
	QPushButton* move_layer_down_button;
	QLineEdit* layer_name_edit;
	QCheckBox* layer_visible_checkbox;
	QCheckBox* layer_locked_checkbox;
	QDoubleSpinBox* layer_opacity_spin;

	QComboBox* render_mode_combo;
	QCheckBox* smooth_normals_checkbox;
	QDoubleSpinBox* surface_threshold_spin;
	QPushButton* rebuild_mesh_button;

	QPushButton* selection_copy_button;
	QPushButton* selection_paste_button;
	QPushButton* selection_delete_button;
	QPushButton* selection_duplicate_button;
	QPushButton* selection_move_button;
	QPushButton* selection_clear_button;
	QSpinBox* selection_offset_x_spin;
	QSpinBox* selection_offset_y_spin;
	QSpinBox* selection_offset_z_spin;

	QSpinBox* procedural_seed_spin;
	QPushButton* procedural_random_seed_button;
	QSpinBox* procedural_origin_x_spin;
	QSpinBox* procedural_origin_y_spin;
	QSpinBox* procedural_origin_z_spin;
	QSpinBox* procedural_size_x_spin;
	QSpinBox* procedural_size_y_spin;
	QSpinBox* procedural_size_z_spin;
	QSpinBox* procedural_wall_thickness_spin;
	QCheckBox* procedural_hollow_checkbox;
	QCheckBox* procedural_clear_layer_checkbox;
	QDoubleSpinBox* procedural_noise_scale_spin;
	QDoubleSpinBox* procedural_threshold_spin;
	QDoubleSpinBox* procedural_density_spin;
	QSpinBox* procedural_octaves_spin;
	QSpinBox* procedural_detail_spin;
	QSpinBox* procedural_limit_spin;
	QLabel* procedural_metrics_label;
	QPushButton* generate_box_button;
	QPushButton* generate_ellipsoid_button;
	QPushButton* generate_rock_button;
	QPushButton* generate_terrain_button;
	QPushButton* generate_noise_button;
	QPushButton* generate_crystal_button;
	QPushButton* generate_wall_button;

	std::vector<QPushButton*> exchange_buttons;
};
