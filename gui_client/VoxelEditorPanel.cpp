/*=====================================================================
VoxelEditorPanel.cpp
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "VoxelEditorPanel.h"
#include "VoxelEditorData.h"
#include "LucideIconUtils.h"
#include "VoxelProceduralGenerator.h"
#include "VoxelUndoStack.h"


#include "../shared/WorldObject.h"
#include "../shared/VoxelMeshBuilding.h"
#include <QtCore/QVariant>
#include <QtCore/QRandomGenerator>
#include <QtCore/QCoreApplication>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QKeyEvent>
#include <QtGui/QStandardItemModel>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>


namespace
{

uint32_t opaqueRGB(const uint32_t rgba)
{
	return rgba | 0xFFu;
}


QColor toQColor(const uint32_t rgba)
{
	return QColor(
		static_cast<int>((rgba >> 24) & 0xFFu),
		static_cast<int>((rgba >> 16) & 0xFFu),
		static_cast<int>((rgba >> 8) & 0xFFu),
		static_cast<int>(rgba & 0xFFu));
}


uint32_t fromQColor(const QColor& colour)
{
	return
		(static_cast<uint32_t>(colour.red()) << 24) |
		(static_cast<uint32_t>(colour.green()) << 16) |
		(static_cast<uint32_t>(colour.blue()) << 8) |
		0xFFu;
}


uint8_t floatToByte(const float value)
{
	if(!std::isfinite(value))
		return 0;
	return static_cast<uint8_t>(std::lround(std::max(0.0f, std::min(1.0f, value)) * 255.0f));
}


uint32_t materialRGB(const WorldMaterial& material)
{
	return
		(static_cast<uint32_t>(floatToByte(material.colour_rgb.r)) << 24) |
		(static_cast<uint32_t>(floatToByte(material.colour_rgb.g)) << 16) |
		(static_cast<uint32_t>(floatToByte(material.colour_rgb.b)) << 8) |
		0xFFu;
}


void normalisePalette(std::vector<uint32_t>& colours)
{
	for(uint32_t& colour : colours)
		colour = opaqueRGB(colour);
	std::vector<uint32_t> unique;
	unique.reserve(colours.size());
	for(const uint32_t colour : colours)
		if(std::find(unique.begin(), unique.end(), colour) == unique.end())
			unique.push_back(colour);
	colours.swap(unique);
}


void clearGrid(QGridLayout* grid)
{
	while(grid && grid->count() > 0)
	{
		QLayoutItem* item = grid->takeAt(0);
		if(item->widget())
			delete item->widget();
		delete item;
	}
}


QString layerDisplayName(const VoxelLayer& layer)
{
	const QString visible = layer.visible ? QString::fromUtf8("\xE2\x97\x89") : QString::fromUtf8("\xE2\x97\x8B");
	const QString locked = layer.locked ? QString::fromUtf8(" \xF0\x9F\x94\x92") : QString();
	return visible + locked + QStringLiteral("  ") + QString::fromUtf8(layer.name.c_str());
}


bool containsInt(const std::vector<int>& values, const int value)
{
	return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace


VoxelEditorPanel::VoxelEditorPanel(QWidget* parent)
:	QWidget(parent),
	editor_state(VoxelEditorData::defaultForMaterialCount(1)),
	selected_tool(VoxelToolType::Brush),
	updating(false),
	editable(false),
	loaded_editor_metadata(false),
	editor_metadata_dirty(false),
	pending_colour_material(false),
	layer_render_dirty(false),
	info_label(NULL),
	voxel_count_label(NULL),
	scene_tools_checkbox(NULL),
	tool_button_group(NULL),
	tool_settings_widget(NULL),
	brush_size_spin(NULL),
	brush_shape_combo(NULL),
	brush_mode_combo(NULL),
	hollow_checkbox(NULL),
	mirror_x_checkbox(NULL),
	mirror_y_checkbox(NULL),
	mirror_z_checkbox(NULL),
	current_colour_button(NULL),
	palette_widget(NULL),
	palette_grid(NULL),
	recent_label(NULL),
	recent_grid(NULL),
	layer_list(NULL),
	add_layer_button(NULL),
	delete_layer_button(NULL),
	move_layer_up_button(NULL),
	move_layer_down_button(NULL),
	layer_name_edit(NULL),
	layer_visible_checkbox(NULL),
	layer_locked_checkbox(NULL),
	layer_opacity_spin(NULL),
	render_mode_combo(NULL),
	smooth_normals_checkbox(NULL),
	surface_threshold_spin(NULL),
	rebuild_mesh_button(NULL),
	selection_copy_button(NULL),
	selection_paste_button(NULL),
	selection_delete_button(NULL),
	selection_duplicate_button(NULL),
	selection_move_button(NULL),
	selection_clear_button(NULL),
	selection_offset_x_spin(NULL),
	selection_offset_y_spin(NULL),
	selection_offset_z_spin(NULL),
	procedural_seed_spin(NULL),
	procedural_random_seed_button(NULL),
	procedural_origin_x_spin(NULL),
	procedural_origin_y_spin(NULL),
	procedural_origin_z_spin(NULL),
	procedural_size_x_spin(NULL),
	procedural_size_y_spin(NULL),
	procedural_size_z_spin(NULL),
	procedural_wall_thickness_spin(NULL),
	procedural_hollow_checkbox(NULL),
	procedural_clear_layer_checkbox(NULL),
	procedural_noise_scale_spin(NULL),
	procedural_threshold_spin(NULL),
	procedural_density_spin(NULL),
	procedural_octaves_spin(NULL),
	procedural_detail_spin(NULL),
	procedural_limit_spin(NULL),
	procedural_metrics_label(NULL),
	generate_box_button(NULL),
	generate_ellipsoid_button(NULL),
	generate_rock_button(NULL),
	generate_terrain_button(NULL),
	generate_noise_button(NULL),
	generate_crystal_button(NULL),
	generate_wall_button(NULL)
{
	createUi();
	refreshAllControls();
	setEditable(false);
}


void VoxelEditorPanel::createUi()
{
	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(7);

	QLabel* title = new QLabel(tr("Voxel Editor"), this);
	QFont title_font = title->font();
	title_font.setBold(true);
	title->setFont(title_font);
	root->addWidget(title);

	info_label = new QLabel(tr("Choose a tool, then click a face of the selected voxel object. Changes are committed as one undo command per operation."), this);
	info_label->setWordWrap(true);
	root->addWidget(info_label);
	voxel_count_label = new QLabel(this);
	root->addWidget(voxel_count_label);

	QGroupBox* tools_box = new QGroupBox(tr("Tools"), this);
	QVBoxLayout* tools_layout = new QVBoxLayout(tools_box);
	scene_tools_checkbox = new QCheckBox(tr("Edit voxels in the world"), tools_box);
	scene_tools_checkbox->setChecked(true);
	scene_tools_checkbox->setToolTip(tr("When enabled, clicks on the selected voxel object use the active voxel tool."));
	tools_layout->addWidget(scene_tools_checkbox);

	QGridLayout* buttons = new QGridLayout();
	tool_button_group = new QButtonGroup(this);
	tool_button_group->setExclusive(true);
	const struct { VoxelToolType tool; const char* text; const char* tip; } tool_specs[] = {
		{VoxelToolType::Brush,  QT_TR_NOOP("Brush (B)"),  QT_TR_NOOP("Add or replace voxels with the selected colour.")},
		{VoxelToolType::Eraser, QT_TR_NOOP("Eraser (E)"), QT_TR_NOOP("Remove voxels with the selected brush shape.")},
		{VoxelToolType::Paint,  QT_TR_NOOP("Paint (P)"),  QT_TR_NOOP("Recolour existing voxels without creating new ones.")},
		{VoxelToolType::Line,   QT_TR_NOOP("Line (L)"),   QT_TR_NOOP("Create a brush-shaped 3D line between two points.")},
		{VoxelToolType::Box,    QT_TR_NOOP("Box"),        QT_TR_NOOP("Create a filled or hollow box between two points.")},
		{VoxelToolType::Sphere, QT_TR_NOOP("Sphere"),     QT_TR_NOOP("Create a filled or hollow sphere between two points.")},
		{VoxelToolType::Fill,   QT_TR_NOOP("Fill (F)"),   QT_TR_NOOP("Recolour a bounded 6-connected region of existing voxels.")},
		{VoxelToolType::Picker, QT_TR_NOOP("Picker (I)"), QT_TR_NOOP("Pick the material colour of a voxel.")},
		{VoxelToolType::Select, QT_TR_NOOP("Select (S)"), QT_TR_NOOP("Choose two corners of a voxel region for clipboard operations.")}
	};
	for(int i=0; i<9; ++i)
	{
		QPushButton* button = new QPushButton(tr(tool_specs[i].text), tools_box);
		button->setCheckable(true);
		button->setToolTip(tr(tool_specs[i].tip));
		tool_button_group->addButton(button, static_cast<int>(tool_specs[i].tool));
		buttons->addWidget(button, i / 3, i % 3);
	}
	tools_layout->addLayout(buttons);

	tool_settings_widget = new QWidget(tools_box);
	QFormLayout* tool_form = new QFormLayout(tool_settings_widget);
	brush_size_spin = new QSpinBox(tool_settings_widget);
	brush_size_spin->setRange(1, 16);
	brush_size_spin->setValue(1);
	brush_size_spin->setToolTip(tr("Safety-limited brush diameter in voxels. Use [ and ] to change it."));
	tool_form->addRow(tr("Size"), brush_size_spin);
	brush_shape_combo = new QComboBox(tool_settings_widget);
	brush_shape_combo->addItem(tr("Cube"), static_cast<int>(VoxelBrushShape::Cube));
	brush_shape_combo->addItem(tr("Sphere"), static_cast<int>(VoxelBrushShape::Sphere));
	tool_form->addRow(tr("Brush shape"), brush_shape_combo);
	brush_mode_combo = new QComboBox(tool_settings_widget);
	brush_mode_combo->addItem(tr("Add"), static_cast<int>(VoxelBrushMode::Add));
	brush_mode_combo->addItem(tr("Replace"), static_cast<int>(VoxelBrushMode::Replace));
	brush_mode_combo->addItem(tr("Paint existing"), static_cast<int>(VoxelBrushMode::Paint));
	tool_form->addRow(tr("Brush mode"), brush_mode_combo);
	hollow_checkbox = new QCheckBox(tr("Hollow shape"), tool_settings_widget);
	tool_form->addRow(QString(), hollow_checkbox);
	QWidget* mirror_widget = new QWidget(tool_settings_widget);
	QHBoxLayout* mirror_layout = new QHBoxLayout(mirror_widget);
	mirror_layout->setContentsMargins(0, 0, 0, 0);
	mirror_x_checkbox = new QCheckBox(QStringLiteral("X"), mirror_widget);
	mirror_y_checkbox = new QCheckBox(QStringLiteral("Y"), mirror_widget);
	mirror_z_checkbox = new QCheckBox(QStringLiteral("Z"), mirror_widget);
	mirror_layout->addWidget(mirror_x_checkbox);
	mirror_layout->addWidget(mirror_y_checkbox);
	mirror_layout->addWidget(mirror_z_checkbox);
	mirror_layout->addStretch(1);
	tool_form->addRow(tr("Mirror at origin"), mirror_widget);
	tools_layout->addWidget(tool_settings_widget);
	root->addWidget(tools_box);

	QGroupBox* colour_box = new QGroupBox(tr("24-bit RGB colour"), this);
	QVBoxLayout* colour_layout = new QVBoxLayout(colour_box);
	current_colour_button = new QPushButton(colour_box);
	current_colour_button->setMinimumHeight(30);
	current_colour_button->setToolTip(tr("Choose an RGB colour. Layer opacity is controlled separately."));
	colour_layout->addWidget(current_colour_button);
	colour_layout->addWidget(new QLabel(tr("Palette"), colour_box));
	palette_widget = new QWidget(colour_box);
	palette_grid = new QGridLayout(palette_widget);
	palette_grid->setContentsMargins(0, 0, 0, 0);
	palette_grid->setSpacing(3);
	colour_layout->addWidget(palette_widget);
	recent_label = new QLabel(tr("Recent"), colour_box);
	colour_layout->addWidget(recent_label);
	QWidget* recent_widget = new QWidget(colour_box);
	recent_grid = new QGridLayout(recent_widget);
	recent_grid->setContentsMargins(0, 0, 0, 0);
	recent_grid->setSpacing(3);
	colour_layout->addWidget(recent_widget);
	root->addWidget(colour_box);

	QGroupBox* layers_box = new QGroupBox(tr("Layers"), this);
	QVBoxLayout* layers_layout = new QVBoxLayout(layers_box);
	layer_list = new QListWidget(layers_box);
	layer_list->setMinimumHeight(82);
	layer_list->setToolTip(tr("Stage 1 layers own disjoint material indices and remain compatible with legacy voxel data."));
	layers_layout->addWidget(layer_list);
	QHBoxLayout* layer_buttons = new QHBoxLayout();
	add_layer_button = new QPushButton(QStringLiteral("+"), layers_box);
	delete_layer_button = new QPushButton(QString::fromUtf8("\xE2\x88\x92"), layers_box);
	move_layer_up_button = new QPushButton(QString::fromUtf8("\xE2\x86\x91"), layers_box);
	move_layer_down_button = new QPushButton(QString::fromUtf8("\xE2\x86\x93"), layers_box);
	add_layer_button->setToolTip(tr("Add layer"));
	delete_layer_button->setToolTip(tr("Delete active layer and its voxels"));
	move_layer_up_button->setToolTip(tr("Move active layer up"));
	move_layer_down_button->setToolTip(tr("Move active layer down"));
	layer_buttons->addWidget(add_layer_button);
	layer_buttons->addWidget(delete_layer_button);
	layer_buttons->addWidget(move_layer_up_button);
	layer_buttons->addWidget(move_layer_down_button);
	layer_buttons->addStretch(1);
	layers_layout->addLayout(layer_buttons);
	QFormLayout* layer_form = new QFormLayout();
	layer_name_edit = new QLineEdit(layers_box);
	layer_name_edit->setMaxLength(64);
	layer_form->addRow(tr("Name"), layer_name_edit);
	layer_visible_checkbox = new QCheckBox(tr("Visible"), layers_box);
	layer_locked_checkbox = new QCheckBox(tr("Locked"), layers_box);
	QWidget* layer_flags = new QWidget(layers_box);
	QHBoxLayout* flags_layout = new QHBoxLayout(layer_flags);
	flags_layout->setContentsMargins(0, 0, 0, 0);
	flags_layout->addWidget(layer_visible_checkbox);
	flags_layout->addWidget(layer_locked_checkbox);
	flags_layout->addStretch(1);
	layer_form->addRow(tr("State"), layer_flags);
	layer_opacity_spin = new QDoubleSpinBox(layers_box);
	layer_opacity_spin->setRange(0.0, 1.0);
	layer_opacity_spin->setSingleStep(0.05);
	layer_opacity_spin->setDecimals(2);
	layer_form->addRow(tr("Opacity"), layer_opacity_spin);
	layers_layout->addLayout(layer_form);
	root->addWidget(layers_box);

	QGroupBox* selection_box = new QGroupBox(tr("Selection / Clipboard"), this);
	QVBoxLayout* selection_layout = new QVBoxLayout(selection_box);
	QLabel* selection_help = new QLabel(tr("Choose Select, then click two corners in the world."), selection_box);
	selection_help->setWordWrap(true);
	selection_layout->addWidget(selection_help);
	QGridLayout* selection_buttons = new QGridLayout();
	selection_copy_button = new QPushButton(tr("Copy"), selection_box);
	selection_paste_button = new QPushButton(tr("Paste"), selection_box);
	selection_delete_button = new QPushButton(tr("Delete"), selection_box);
	selection_duplicate_button = new QPushButton(tr("Duplicate"), selection_box);
	selection_move_button = new QPushButton(tr("Move"), selection_box);
	selection_clear_button = new QPushButton(tr("Clear selection"), selection_box);
	selection_buttons->addWidget(selection_copy_button, 0, 0);
	selection_buttons->addWidget(selection_paste_button, 0, 1);
	selection_buttons->addWidget(selection_delete_button, 1, 0);
	selection_buttons->addWidget(selection_duplicate_button, 1, 1);
	selection_buttons->addWidget(selection_move_button, 2, 0);
	selection_buttons->addWidget(selection_clear_button, 2, 1);
	selection_layout->addLayout(selection_buttons);
	QFormLayout* selection_form = new QFormLayout();
	QWidget* offset_widget = new QWidget(selection_box);
	QHBoxLayout* offset_layout = new QHBoxLayout(offset_widget);
	offset_layout->setContentsMargins(0, 0, 0, 0);
	selection_offset_x_spin = new QSpinBox(offset_widget);
	selection_offset_y_spin = new QSpinBox(offset_widget);
	selection_offset_z_spin = new QSpinBox(offset_widget);
	for(QSpinBox* spin : { selection_offset_x_spin, selection_offset_y_spin, selection_offset_z_spin })
	{
		spin->setRange(-256, 256);
		spin->setMinimumWidth(58);
	}
	selection_offset_x_spin->setValue(1);
	offset_layout->addWidget(new QLabel(QStringLiteral("X"), offset_widget));
	offset_layout->addWidget(selection_offset_x_spin);
	offset_layout->addWidget(new QLabel(QStringLiteral("Y"), offset_widget));
	offset_layout->addWidget(selection_offset_y_spin);
	offset_layout->addWidget(new QLabel(QStringLiteral("Z"), offset_widget));
	offset_layout->addWidget(selection_offset_z_spin);
	selection_form->addRow(tr("Paste / move offset"), offset_widget);
	selection_layout->addLayout(selection_form);
	root->addWidget(selection_box);

	QGroupBox* rendering_box = new QGroupBox(tr("Rendering"), this);
	QFormLayout* rendering_form = new QFormLayout(rendering_box);
	render_mode_combo = new QComboBox(rendering_box);
	render_mode_combo->addItem(tr("Greedy (optimised)"), static_cast<int>(VoxelRenderMode::Greedy));
	render_mode_combo->addItem(tr("Cubes"), static_cast<int>(VoxelRenderMode::Cubes));
	render_mode_combo->addItem(tr("Marching Cubes (TODO)"), static_cast<int>(VoxelRenderMode::MarchingCubes));
	if(QStandardItemModel* model = qobject_cast<QStandardItemModel*>(render_mode_combo->model()))
		if(QStandardItem* item = model->item(2))
		{
			item->setEnabled(false);
			item->setToolTip(tr("Not implemented in stage 1; voxel data will not be converted or lost."));
		}
	rendering_form->addRow(tr("Mode"), render_mode_combo);
	smooth_normals_checkbox = new QCheckBox(tr("Smooth normals (requires Marching Cubes)"), rendering_box);
	smooth_normals_checkbox->setEnabled(false);
	rendering_form->addRow(QString(), smooth_normals_checkbox);
	surface_threshold_spin = new QDoubleSpinBox(rendering_box);
	surface_threshold_spin->setRange(0.0, 1.0);
	surface_threshold_spin->setSingleStep(0.05);
	surface_threshold_spin->setDecimals(2);
	surface_threshold_spin->setEnabled(false);
	rendering_form->addRow(tr("Surface threshold (TODO)"), surface_threshold_spin);
	rebuild_mesh_button = new QPushButton(tr("Rebuild mesh"), rendering_box);
	rendering_form->addRow(QString(), rebuild_mesh_button);
	root->addWidget(rendering_box);

	QGroupBox* procedural_box = new QGroupBox(tr("Procedural"), this);
	QVBoxLayout* procedural_root = new QVBoxLayout(procedural_box);
	QFormLayout* procedural_form = new QFormLayout();
	QWidget* seed_widget = new QWidget(procedural_box);
	QHBoxLayout* seed_layout = new QHBoxLayout(seed_widget);
	seed_layout->setContentsMargins(0, 0, 0, 0);
	procedural_seed_spin = new QSpinBox(seed_widget);
	procedural_seed_spin->setRange(0, std::numeric_limits<int>::max());
	procedural_seed_spin->setValue(12345);
	procedural_random_seed_button = new QPushButton(tr("Random seed"), seed_widget);
	seed_layout->addWidget(procedural_seed_spin, 1);
	seed_layout->addWidget(procedural_random_seed_button);
	procedural_form->addRow(tr("Seed"), seed_widget);

	const auto make_xyz_row = [procedural_box](QSpinBox*& x, QSpinBox*& y, QSpinBox*& z,
		const int minimum, const int maximum, const int x_value, const int y_value, const int z_value)
	{
		QWidget* widget = new QWidget(procedural_box);
		QHBoxLayout* layout = new QHBoxLayout(widget);
		layout->setContentsMargins(0, 0, 0, 0);
		x = new QSpinBox(widget); y = new QSpinBox(widget); z = new QSpinBox(widget);
		QSpinBox* spins[] = { x, y, z };
		const int values[] = { x_value, y_value, z_value };
		const char* labels[] = { "X", "Y", "Z" };
		for(int i=0; i<3; ++i)
		{
			spins[i]->setRange(minimum, maximum);
			spins[i]->setValue(values[i]);
			spins[i]->setMinimumWidth(58);
			layout->addWidget(new QLabel(QString::fromLatin1(labels[i]), widget));
			layout->addWidget(spins[i]);
		}
		return widget;
	};
	procedural_form->addRow(tr("Origin X/Y/Z"), make_xyz_row(procedural_origin_x_spin, procedural_origin_y_spin,
		procedural_origin_z_spin, -32768, 32766, -6, -6, 0));
	procedural_form->addRow(tr("Size X/Y/Z"), make_xyz_row(procedural_size_x_spin, procedural_size_y_spin,
		procedural_size_z_spin, 1, 128, 12, 12, 12));
	procedural_wall_thickness_spin = new QSpinBox(procedural_box);
	procedural_wall_thickness_spin->setRange(1, VoxelProceduralGenerator::MAX_WALL_THICKNESS);
	procedural_wall_thickness_spin->setValue(1);
	procedural_form->addRow(tr("Wall thickness"), procedural_wall_thickness_spin);
	QWidget* procedural_flags = new QWidget(procedural_box);
	QHBoxLayout* procedural_flags_layout = new QHBoxLayout(procedural_flags);
	procedural_flags_layout->setContentsMargins(0, 0, 0, 0);
	procedural_hollow_checkbox = new QCheckBox(tr("Hollow"), procedural_flags);
	procedural_clear_layer_checkbox = new QCheckBox(tr("Clear active layer"), procedural_flags);
	procedural_clear_layer_checkbox->setChecked(true);
	procedural_flags_layout->addWidget(procedural_hollow_checkbox);
	procedural_flags_layout->addWidget(procedural_clear_layer_checkbox);
	procedural_flags_layout->addStretch(1);
	procedural_form->addRow(tr("Mode"), procedural_flags);
	procedural_noise_scale_spin = new QDoubleSpinBox(procedural_box);
	procedural_noise_scale_spin->setRange(0.01, 4.0);
	procedural_noise_scale_spin->setDecimals(3);
	procedural_noise_scale_spin->setSingleStep(0.01);
	procedural_noise_scale_spin->setValue(0.12);
	procedural_form->addRow(tr("Noise scale"), procedural_noise_scale_spin);
	procedural_threshold_spin = new QDoubleSpinBox(procedural_box);
	procedural_threshold_spin->setRange(0.0, 1.0);
	procedural_threshold_spin->setDecimals(2);
	procedural_threshold_spin->setSingleStep(0.05);
	procedural_threshold_spin->setValue(0.5);
	procedural_form->addRow(tr("Threshold"), procedural_threshold_spin);
	procedural_density_spin = new QDoubleSpinBox(procedural_box);
	procedural_density_spin->setRange(0.0, 1.0);
	procedural_density_spin->setDecimals(2);
	procedural_density_spin->setSingleStep(0.05);
	procedural_density_spin->setValue(1.0);
	procedural_form->addRow(tr("Density"), procedural_density_spin);
	procedural_octaves_spin = new QSpinBox(procedural_box);
	procedural_octaves_spin->setRange(1, 8);
	procedural_octaves_spin->setValue(3);
	procedural_form->addRow(tr("Noise octaves"), procedural_octaves_spin);
	procedural_detail_spin = new QSpinBox(procedural_box);
	procedural_detail_spin->setRange(1, 16);
	procedural_detail_spin->setValue(4);
	procedural_form->addRow(tr("Detail"), procedural_detail_spin);
	procedural_limit_spin = new QSpinBox(procedural_box);
	procedural_limit_spin->setRange(100, static_cast<int>(VoxelProceduralGenerator::MAX_GENERATED_VOXELS));
	procedural_limit_spin->setSingleStep(1000);
	procedural_limit_spin->setValue(262144);
	procedural_form->addRow(tr("Safety limit"), procedural_limit_spin);
	procedural_root->addLayout(procedural_form);
	procedural_metrics_label = new QLabel(procedural_box);
	procedural_metrics_label->setWordWrap(true);
	procedural_root->addWidget(procedural_metrics_label);
	QGridLayout* procedural_layout = new QGridLayout();
	generate_box_button = new QPushButton(tr("Box"), procedural_box);
	generate_ellipsoid_button = new QPushButton(tr("Ellipsoid"), procedural_box);
	generate_rock_button = new QPushButton(tr("Rock"), procedural_box);
	generate_terrain_button = new QPushButton(tr("Terrain patch"), procedural_box);
	generate_noise_button = new QPushButton(tr("Noise volume"), procedural_box);
	generate_crystal_button = new QPushButton(tr("Crystal"), procedural_box);
	generate_wall_button = new QPushButton(tr("Wall / ruins"), procedural_box);
	QPushButton* generator_buttons[] = { generate_box_button, generate_ellipsoid_button, generate_rock_button,
		generate_terrain_button, generate_noise_button, generate_crystal_button, generate_wall_button };
	for(int i=0; i<7; ++i)
		procedural_layout->addWidget(generator_buttons[i], i / 2, i % 2);
	procedural_root->addLayout(procedural_layout);
	root->addWidget(procedural_box);

	QGroupBox* exchange_box = new QGroupBox(tr("Import / Export"), this);
	QGridLayout* exchange_layout = new QGridLayout(exchange_box);
	const char* exchange_items[] = { QT_TR_NOOP("Import VOX (TODO)"), QT_TR_NOOP("Export VOX (TODO)"), QT_TR_NOOP("Export OBJ (TODO)"), QT_TR_NOOP("Save asset (TODO)") };
	for(int i=0; i<4; ++i)
	{
		QPushButton* button = new QPushButton(tr(exchange_items[i]), exchange_box);
		button->setEnabled(false);
		button->setToolTip(tr("Not implemented in the first integration stage."));
		exchange_layout->addWidget(button, i / 2, i % 2);
		exchange_buttons.push_back(button);
	}
	root->addWidget(exchange_box);
	root->addStretch(1);

	connect(tool_button_group, SIGNAL(buttonClicked(int)), this, SLOT(toolButtonClicked(int)));
	connect(scene_tools_checkbox, SIGNAL(toggled(bool)), this, SLOT(toolControlChanged()));
	connect(brush_size_spin, SIGNAL(valueChanged(int)), this, SLOT(toolControlChanged()));
	connect(brush_shape_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(toolControlChanged()));
	connect(brush_mode_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(toolControlChanged()));
	connect(hollow_checkbox, SIGNAL(toggled(bool)), this, SLOT(toolControlChanged()));
	connect(mirror_x_checkbox, SIGNAL(toggled(bool)), this, SLOT(toolControlChanged()));
	connect(mirror_y_checkbox, SIGNAL(toggled(bool)), this, SLOT(toolControlChanged()));
	connect(mirror_z_checkbox, SIGNAL(toggled(bool)), this, SLOT(toolControlChanged()));
	connect(current_colour_button, SIGNAL(clicked()), this, SLOT(chooseColour()));
	connect(layer_list, SIGNAL(currentRowChanged(int)), this, SLOT(layerSelectionChanged(int)));
	connect(layer_name_edit, SIGNAL(editingFinished()), this, SLOT(layerNameEdited()));
	connect(layer_visible_checkbox, SIGNAL(toggled(bool)), this, SLOT(layerVisibleChanged(bool)));
	connect(layer_locked_checkbox, SIGNAL(toggled(bool)), this, SLOT(layerLockedChanged(bool)));
	connect(layer_opacity_spin, SIGNAL(valueChanged(double)), this, SLOT(layerOpacityChanged(double)));
	connect(add_layer_button, SIGNAL(clicked()), this, SLOT(addLayer()));
	connect(delete_layer_button, SIGNAL(clicked()), this, SLOT(deleteLayer()));
	connect(move_layer_up_button, SIGNAL(clicked()), this, SLOT(moveLayerUp()));
	connect(move_layer_down_button, SIGNAL(clicked()), this, SLOT(moveLayerDown()));
	connect(render_mode_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(renderModeChanged(int)));
	connect(rebuild_mesh_button, SIGNAL(clicked()), this, SLOT(rebuildMesh()));
	connect(selection_copy_button, SIGNAL(clicked()), this, SIGNAL(selectionCopyRequested()));
	connect(selection_paste_button, SIGNAL(clicked()), this, SIGNAL(selectionPasteRequested()));
	connect(selection_delete_button, SIGNAL(clicked()), this, SIGNAL(selectionDeleteRequested()));
	connect(selection_duplicate_button, SIGNAL(clicked()), this, SIGNAL(selectionDuplicateRequested()));
	connect(selection_move_button, SIGNAL(clicked()), this, SIGNAL(selectionMoveRequested()));
	connect(selection_clear_button, SIGNAL(clicked()), this, SIGNAL(selectionClearRequested()));
	connect(procedural_random_seed_button, SIGNAL(clicked()), this, SLOT(randomiseProceduralSeed()));
	for(QSpinBox* spin : { procedural_seed_spin, procedural_origin_x_spin, procedural_origin_y_spin,
		procedural_origin_z_spin, procedural_size_x_spin, procedural_size_y_spin, procedural_size_z_spin,
		procedural_wall_thickness_spin, procedural_octaves_spin, procedural_detail_spin, procedural_limit_spin })
		connect(spin, SIGNAL(valueChanged(int)), this, SLOT(proceduralControlChanged()));
	for(QDoubleSpinBox* spin : { procedural_noise_scale_spin, procedural_threshold_spin, procedural_density_spin })
		connect(spin, SIGNAL(valueChanged(double)), this, SLOT(proceduralControlChanged()));
	connect(procedural_hollow_checkbox, SIGNAL(toggled(bool)), this, SLOT(proceduralControlChanged()));
	connect(procedural_clear_layer_checkbox, SIGNAL(toggled(bool)), this, SLOT(proceduralControlChanged()));
	connect(generate_box_button, &QPushButton::clicked, this, [this]() { emit proceduralGenerationRequested(static_cast<int>(VoxelProceduralType::Box)); });
	connect(generate_ellipsoid_button, &QPushButton::clicked, this, [this]() { emit proceduralGenerationRequested(static_cast<int>(VoxelProceduralType::Ellipsoid)); });
	connect(generate_rock_button, &QPushButton::clicked, this, [this]() { emit proceduralGenerationRequested(static_cast<int>(VoxelProceduralType::Rock)); });
	connect(generate_terrain_button, &QPushButton::clicked, this, [this]() { emit proceduralGenerationRequested(static_cast<int>(VoxelProceduralType::TerrainPatch)); });
	connect(generate_noise_button, &QPushButton::clicked, this, [this]() { emit proceduralGenerationRequested(static_cast<int>(VoxelProceduralType::NoiseVolume)); });
	connect(generate_crystal_button, &QPushButton::clicked, this, [this]() { emit proceduralGenerationRequested(static_cast<int>(VoxelProceduralType::Crystal)); });
	connect(generate_wall_button, &QPushButton::clicked, this, [this]() { emit proceduralGenerationRequested(static_cast<int>(VoxelProceduralType::Wall)); });
	refreshProceduralMetrics();
}


void VoxelEditorPanel::setFromObject(const WorldObject& object)
{
	updating = true;
	loaded_editor_metadata = VoxelEditorData::isVoxelEditorContent(object.content);
	editor_metadata_dirty = false;
	std::string parse_error;
	bool migrated = false;
	editor_state = VoxelEditorData::fromObject(object, &parse_error, &migrated);
	editor_state.palette.current_colour = opaqueRGB(editor_state.palette.current_colour);
	normalisePalette(editor_state.palette.colours);
	normalisePalette(editor_state.palette.recent_colours);
	rebuildMaterialColourCache(object);
	removed_material_indices.clear();
	layer_render_dirty = false;

	const int current = editor_state.current_material_index;
	if(current >= 0 && current < static_cast<int>(material_colours.size()) &&
		VoxelEditorData::layerOwnsMaterial(editor_state, editor_state.active_layer, current))
	{
		editor_state.palette.current_colour = material_colours[current];
		pending_colour_material = false;
	}
	else
	{
		const VoxelLayer* layer = VoxelEditorData::activeLayer(editor_state);
		if(layer && !layer->material_indices.empty() && layer->material_indices[0] < static_cast<int>(material_colours.size()))
		{
			editor_state.current_material_index = layer->material_indices[0];
			editor_state.palette.current_colour = material_colours[editor_state.current_material_index];
			pending_colour_material = false;
		}
		else
			pending_colour_material = true;
	}

	if(!parse_error.empty())
		info_label->setText(tr("Voxel metadata could not be read; safe defaults are shown: %1").arg(QString::fromUtf8(parse_error.c_str())));
	else if(migrated)
		info_label->setText(tr("Legacy voxel object. Editor metadata will be added when you make the first edit."));
	else if(editor_state.render_mode == VoxelRenderMode::MarchingCubes)
		info_label->setText(tr("Marching Cubes metadata was preserved, but stage 1 renders this object with the non-destructive fallback."));
	else
		info_label->setText(tr("Choose a tool, then click a face of the selected voxel object. Changes are committed as one undo command per operation."));
	updateVoxelCount(object.getDecompressedVoxels().size());
	updating = false;
	refreshAllControls();
}


bool VoxelEditorPanel::applyToObject(WorldObject& object, std::string& error_out)
{
	error_out.clear();
	if(object.object_type != WorldObject::ObjectType_VoxelGroup)
	{
		error_out = "Voxel editor can only edit voxel-group objects.";
		return false;
	}
	if(object.getCompressedVoxels().nonNull() && !object.getCompressedVoxels()->empty())
		object.decompressVoxels();

	VoxelEditorData::clamp(editor_state);
	// Merely editing a legacy voxel's generic physics/audio/material/content
	// controls must not replace its arbitrary WorldObject::content with editor
	// metadata.  Migrate only after a specialised voxel-editor state change.
	if(!loaded_editor_metadata && !editor_metadata_dirty)
	{
		editor_state.legacy_content = object.content;
		rebuildMaterialColourCache(object);
		updateVoxelCount(object.getDecompressedVoxels().size());
		return true;
	}

	// If the generic material editor changed scalar opacity, treat that as the
	// new non-destructive base opacity.  A layer visibility/opacity edit, on the
	// other hand, deliberately changes only the layer multiplier below.
	if(!layer_render_dirty)
	{
		for(size_t layer_i=0; layer_i<editor_state.layers.size(); ++layer_i)
		{
			const VoxelLayer& layer = editor_state.layers[layer_i];
			for(const int index : layer.material_indices)
				if(index >= 0 && index < static_cast<int>(object.materials.size()) && object.materials[index].nonNull())
				{
					const float old_base = VoxelEditorData::materialBaseOpacity(editor_state, static_cast<int>(layer_i), index);
					const float expected = layer.visible ? old_base * layer.opacity : 0.0f;
					const float actual = object.materials[index]->opacity.val;
					if(std::fabs(actual - expected) > 0.0001f)
					{
						const float new_base = (layer.visible && layer.opacity > 0.0001f) ? actual / layer.opacity : actual;
						VoxelEditorData::setMaterialBaseOpacity(editor_state, static_cast<int>(layer_i), index, new_base);
					}
				}
		}
	}

	const uint32_t wanted_colour = opaqueRGB(editor_state.palette.current_colour);
	int material_index = findMaterialForColourInActiveLayer(wanted_colour);
	if(!pending_colour_material && editor_state.current_material_index >= 0 &&
		editor_state.current_material_index < static_cast<int>(object.materials.size()) &&
		VoxelEditorData::layerOwnsMaterial(editor_state, editor_state.active_layer, editor_state.current_material_index))
		material_index = editor_state.current_material_index;

	bool created_material = false;
	if(material_index < 0)
	{
		// Reuse a material slot released by a deleted layer (or another unused,
		// unclaimed slot) before growing the legacy index-addressed array.
		int reusable_index = -1;
		for(const int index : removed_material_indices)
			if(index >= 0 && index < static_cast<int>(object.materials.size()))
			{
				reusable_index = index;
				break;
			}
		if(reusable_index < 0)
			for(size_t i=0; i<object.materials.size(); ++i)
			{
				bool referenced = false;
				for(const Voxel& voxel : object.getDecompressedVoxels())
					if(voxel.mat_index == static_cast<int>(i))
					{
						referenced = true;
						break;
					}
				if(!referenced && VoxelEditorData::materialLayerIndex(editor_state, static_cast<int>(i)) < 0)
				{
					reusable_index = static_cast<int>(i);
					break;
				}
			}

		if(reusable_index < 0 && object.materials.size() > static_cast<size_t>(VoxelEditorData::MAX_MATERIAL_INDEX))
		{
			error_out = "Voxel material limit (255) reached; remove unused materials before adding another colour.";
			return false;
		}
		WorldMaterialRef material = new WorldMaterial();
		const QColor colour = toQColor(wanted_colour);
		material->name = std::string("Voxel ") + colour.name(QColor::HexRgb).toStdString();
		material->colour_rgb = Colour3f(colour.redF(), colour.greenF(), colour.blueF());
		const VoxelLayer* layer = VoxelEditorData::activeLayer(editor_state);
		material->opacity.val = (layer && layer->visible) ? layer->opacity : 0.0f;
		if(reusable_index >= 0)
		{
			object.materials[reusable_index] = material;
			material_index = reusable_index;
		}
		else
		{
			object.materials.push_back(material);
			material_index = static_cast<int>(object.materials.size()) - 1;
		}
		created_material = true;
	}

	if(!VoxelEditorData::ensureMaterialInLayer(editor_state, editor_state.active_layer, material_index))
	{
		error_out = "Could not assign the selected material to the active voxel layer.";
		return false;
	}
	if(created_material)
		VoxelEditorData::setMaterialBaseOpacity(editor_state, editor_state.active_layer, material_index, 1.0f);
	editor_state.current_material_index = material_index;
	editor_state.palette.current_colour = wanted_colour;
	pending_colour_material = false;

	// Delete layer data only after all operations that may reject this edit have
	// passed.  This keeps applyToObject() non-destructive on validation errors.
	bool voxel_payload_changed = false;
	if(!removed_material_indices.empty())
	{
		auto& voxels = object.getDecompressedVoxels();
		const bool had_voxels = !voxels.empty();
		const Vec3<int> fallback_position = had_voxels ? voxels[0].pos : Vec3<int>(0, 0, 0);
		size_t write_i = 0;
		for(size_t read_i=0; read_i<voxels.size(); ++read_i)
			if(containsInt(removed_material_indices, voxels[read_i].mat_index))
				voxel_payload_changed = true;
			else
			{
				if(write_i != read_i)
					voxels[write_i] = voxels[read_i];
				++write_i;
			}
		voxels.resize(write_i);
		// The legacy voxel renderer cannot load an empty group.  If deleting a
		// layer removed the complete payload, keep one seed voxel in the active
		// layer; deleting the WorldObject itself remains a separate action.
		if(had_voxels && voxels.empty())
			voxels.push_back(Voxel(fallback_position, material_index));
	}
	if(voxel_payload_changed)
		object.compressVoxels();

	for(size_t layer_i=0; layer_i<editor_state.layers.size(); ++layer_i)
	{
		const VoxelLayer& layer = editor_state.layers[layer_i];
		for(const int index : layer.material_indices)
			if(index >= 0 && index < static_cast<int>(object.materials.size()) && object.materials[index].nonNull())
			{
				const float base_opacity = VoxelEditorData::materialBaseOpacity(editor_state, static_cast<int>(layer_i), index);
				const float target_opacity = layer.visible ? base_opacity * layer.opacity : 0.0f;
				if(std::fabs(object.materials[index]->opacity.val - target_opacity) > 0.0001f)
				{
					WorldMaterialRef clone = object.materials[index]->clone();
					clone->opacity.val = target_opacity;
					object.materials[index] = clone;
				}
			}
	}

	if(!VoxelEditorData::storeOnObject(object, editor_state))
	{
		error_out = "Could not store voxel-editor metadata on the selected object.";
		return false;
	}
	object.changed_flags |= WorldObject::CONTENT_CHANGED;
	removed_material_indices.clear();
	layer_render_dirty = false;
	loaded_editor_metadata = true;
	editor_metadata_dirty = false;
	rebuildMaterialColourCache(object);
	updateVoxelCount(object.getDecompressedVoxels().size());
	refreshAllControls();
	return true;
}


void VoxelEditorPanel::setLegacyContent(const std::string& content)
{
	if(editor_state.legacy_content == content)
		return;
	editor_state.legacy_content = content;
	if(loaded_editor_metadata)
		editor_metadata_dirty = true;
}


void VoxelEditorPanel::setEditable(const bool editable_)
{
	editable = editable_;
	setEnabled(editable);
	refreshLayerList();
	refreshToolControls();
	refreshActiveLayerControls();
}


void VoxelEditorPanel::setIconDirectory(const QString& directory)
{
	icon_directory = directory;
	applyIcons();
}


int VoxelEditorPanel::currentMaterialIndex() const
{
	return editor_state.current_material_index;
}


VoxelToolSettings VoxelEditorPanel::toolSettings() const
{
	VoxelToolSettings settings;
	settings.brush_size = brush_size_spin->value();
	settings.brush_shape = static_cast<VoxelBrushShape>(brush_shape_combo->currentData().toInt());
	settings.brush_mode = static_cast<VoxelBrushMode>(brush_mode_combo->currentData().toInt());
	settings.hollow = hollow_checkbox->isChecked();
	settings.mirror_x = mirror_x_checkbox->isChecked();
	settings.mirror_y = mirror_y_checkbox->isChecked();
	settings.mirror_z = mirror_z_checkbox->isChecked();
	settings.material_index = editor_state.current_material_index;
	settings.layer_index = editor_state.active_layer;
	return settings;
}


VoxelProceduralParams VoxelEditorPanel::proceduralParams(const VoxelProceduralType type) const
{
	VoxelProceduralParams params = VoxelProceduralGenerator::defaultParams(type);
	params.seed = static_cast<uint32_t>(procedural_seed_spin->value());
	params.origin = Vec3<int>(procedural_origin_x_spin->value(), procedural_origin_y_spin->value(), procedural_origin_z_spin->value());
	params.size_x = procedural_size_x_spin->value();
	params.size_y = procedural_size_y_spin->value();
	params.size_z = procedural_size_z_spin->value();
	params.wall_thickness = procedural_wall_thickness_spin->value();
	params.hollow = procedural_hollow_checkbox->isChecked();
	params.clear_active_layer = procedural_clear_layer_checkbox->isChecked();
	params.noise_scale = static_cast<float>(procedural_noise_scale_spin->value());
	params.threshold = static_cast<float>(procedural_threshold_spin->value());
	params.density = static_cast<float>(procedural_density_spin->value());
	params.octaves = procedural_octaves_spin->value();
	params.detail = procedural_detail_spin->value();
	params.max_voxels = static_cast<size_t>(procedural_limit_spin->value());
	params.colour_rgba = editor_state.palette.current_colour;
	params.material_index = editor_state.current_material_index;
	params.layer_index = editor_state.active_layer;
	params.write_mode = static_cast<VoxelBrushMode>(brush_mode_combo->currentData().toInt());
	return params;
}


Vec3<int> VoxelEditorPanel::selectionOffset() const
{
	return Vec3<int>(selection_offset_x_spin->value(), selection_offset_y_spin->value(), selection_offset_z_spin->value());
}


VoxelToolType VoxelEditorPanel::currentTool() const
{
	return selected_tool;
}


bool VoxelEditorPanel::sceneToolsEnabled() const
{
	return editable && scene_tools_checkbox->isChecked() &&
		(selected_tool == VoxelToolType::Picker || selected_tool == VoxelToolType::Select || activeLayerEditable());
}


void VoxelEditorPanel::setSceneToolsEnabled(const bool enabled)
{
	if(scene_tools_checkbox->isChecked() == enabled)
		return;
	updating = true;
	scene_tools_checkbox->setChecked(enabled);
	updating = false;
	emit toolStateChanged();
}


void VoxelEditorPanel::notifyVoxelDataChanged(const WorldObject& object)
{
	updateVoxelCount(object.getDecompressedVoxels().size());
	rebuildMaterialColourCache(object);
}


void VoxelEditorPanel::selectMaterialIndex(const int material_index)
{
	if(material_index < 0 || material_index >= static_cast<int>(material_colours.size()))
		return;
	const int layer_index = VoxelEditorData::materialLayerIndex(editor_state, material_index);
	if(layer_index < 0)
		return;
	editor_state.active_layer = layer_index;
	editor_state.current_material_index = material_index;
	editor_state.palette.current_colour = material_colours[material_index];
	pending_colour_material = false;
	if(std::find(editor_state.palette.recent_colours.begin(), editor_state.palette.recent_colours.end(), material_colours[material_index]) == editor_state.palette.recent_colours.end())
		editor_state.palette.recent_colours.insert(editor_state.palette.recent_colours.begin(), material_colours[material_index]);
	VoxelEditorData::clamp(editor_state);
	refreshAllControls();
	emitMetadataChange();
	emit toolStateChanged();
}


bool VoxelEditorPanel::handleShortcut(QKeyEvent* event)
{
	if(!event || !editable || !scene_tools_checkbox->isChecked())
		return false;
	const Qt::KeyboardModifiers modifiers = event->modifiers();
	if(modifiers != Qt::NoModifier && modifiers != Qt::ShiftModifier)
		return false;

	switch(event->key())
	{
	case Qt::Key_B: selectTool(VoxelToolType::Brush, true); return true;
	case Qt::Key_E: selectTool(VoxelToolType::Eraser, true); return true;
	case Qt::Key_P: selectTool(VoxelToolType::Paint, true); return true;
	case Qt::Key_L: selectTool(VoxelToolType::Line, true); return true;
	case Qt::Key_F: selectTool(VoxelToolType::Fill, true); return true;
	case Qt::Key_I: selectTool(VoxelToolType::Picker, true); return true;
	case Qt::Key_S: selectTool(VoxelToolType::Select, true); return true;
	case Qt::Key_BracketLeft:
		brush_size_spin->setValue(std::max(brush_size_spin->minimum(), brush_size_spin->value() - 1));
		return true;
	case Qt::Key_BracketRight:
		brush_size_spin->setValue(std::min(brush_size_spin->maximum(), brush_size_spin->value() + 1));
		return true;
	default:
		return false;
	}
}


bool VoxelEditorPanel::runSmokeCheck(std::string& report_out)
{
	std::string data_details;
	std::string tool_details;
	std::string undo_details;
	std::string procedural_details;
	const bool data_ok = VoxelEditorData::runSelfTest(&data_details);
	const bool tools_ok = VoxelTools::runSelfTest(&tool_details);
	const bool undo_ok = VoxelUndoStack::runSelfTest(&undo_details);
	const bool procedural_ok = VoxelProceduralGenerator::runSelfTest(&procedural_details);

	WorldObject object;
	object.object_type = WorldObject::ObjectType_VoxelGroup;
	WorldMaterialRef material = new WorldMaterial();
	material->colour_rgb = Colour3f(0.2f, 0.4f, 0.8f);
	object.materials.push_back(material);
	object.getDecompressedVoxels().push_back(Voxel(Vec3<int>(0, 0, 0), 0));

	VoxelEditorPanel panel;
	const QString smoke_icon_dir = LucideIconUtils::directoryForBasePath(QCoreApplication::applicationDirPath().toStdString());
	panel.setIconDirectory(smoke_icon_dir);
	panel.setFromObject(object);
	panel.setEditable(true);
	panel.addLayer();
	std::string apply_error;
	const bool apply_ok = panel.applyToObject(object, apply_error);
	const VoxelEditorState roundtrip = VoxelEditorData::fromObject(object);
	const bool panel_roundtrip_ok = apply_ok && VoxelEditorData::isVoxelEditorContent(object.content) &&
		roundtrip.layers.size() == 2 && object.materials.size() == 2 &&
		panel.currentMaterialIndex() == 1 && object.getDecompressedVoxels().size() == 1;
	object.getDecompressedVoxels().push_back(Voxel(Vec3<int>(1, 0, 0), 1));
	object.compressVoxels();
	panel.setFromObject(object);
	panel.deleteLayer();
	std::string layer_delete_error;
	const bool layer_delete_apply_ok = panel.applyToObject(object, layer_delete_error);
	object.getDecompressedVoxels().clear();
	object.decompressVoxels();
	const VoxelEditorState after_delete = VoxelEditorData::fromObject(object);
	const bool layer_delete_persisted = layer_delete_apply_ok && after_delete.layers.size() == 1 &&
		object.getDecompressedVoxels().size() == 1 && object.getDecompressedVoxels()[0].mat_index == 0;

	WorldObject legacy_object;
	legacy_object.object_type = WorldObject::ObjectType_VoxelGroup;
	legacy_object.content = "legacy voxel content\nkept by the generic editor";
	legacy_object.materials.push_back(new WorldMaterial());
	legacy_object.getDecompressedVoxels().push_back(Voxel(Vec3<int>(0, 0, 0), 0));
	VoxelEditorPanel legacy_panel;
	legacy_panel.setFromObject(legacy_object);
	legacy_panel.setEditable(true);
	std::string legacy_error;
	const bool legacy_generic_apply_ok = legacy_panel.applyToObject(legacy_object, legacy_error) &&
		legacy_object.content == "legacy voxel content\nkept by the generic editor";
	legacy_panel.addLayer();
	const bool legacy_migration_apply_ok = legacy_panel.applyToObject(legacy_object, legacy_error);
	const VoxelEditorState migrated_legacy_state = VoxelEditorData::fromObject(legacy_object);
	const bool legacy_content_preserved = legacy_generic_apply_ok && legacy_migration_apply_ok &&
		VoxelEditorData::isVoxelEditorContent(legacy_object.content) &&
		migrated_legacy_state.legacy_content == "legacy voxel content\nkept by the generic editor";

	WorldObject opacity_object;
	opacity_object.object_type = WorldObject::ObjectType_VoxelGroup;
	WorldMaterialRef opacity_material_a = new WorldMaterial();
	WorldMaterialRef opacity_material_b = new WorldMaterial();
	opacity_material_a->opacity.val = 0.35f;
	opacity_material_b->opacity.val = 0.70f;
	opacity_object.materials.push_back(opacity_material_a);
	opacity_object.materials.push_back(opacity_material_b);
	opacity_object.getDecompressedVoxels().push_back(Voxel(Vec3<int>(0, 0, 0), 0));
	opacity_object.getDecompressedVoxels().push_back(Voxel(Vec3<int>(1, 0, 0), 1));
	VoxelEditorPanel opacity_panel;
	opacity_panel.setFromObject(opacity_object);
	opacity_panel.setEditable(true);
	opacity_panel.layer_visible_checkbox->setChecked(false);
	std::string opacity_error;
	const bool opacity_hide_ok = opacity_panel.applyToObject(opacity_object, opacity_error) &&
		std::fabs(opacity_object.materials[0]->opacity.val) < 0.0001f && std::fabs(opacity_object.materials[1]->opacity.val) < 0.0001f;
	opacity_panel.setFromObject(opacity_object);
	opacity_panel.layer_visible_checkbox->setChecked(true);
	const bool opacity_show_ok = opacity_panel.applyToObject(opacity_object, opacity_error) &&
		std::fabs(opacity_object.materials[0]->opacity.val - 0.35f) < 0.0001f &&
		std::fabs(opacity_object.materials[1]->opacity.val - 0.70f) < 0.0001f;
	const bool base_opacity_preserved = opacity_hide_ok && opacity_show_ok;

	QKeyEvent line_event(QEvent::KeyPress, Qt::Key_L, Qt::NoModifier);
	QKeyEvent fill_event(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier);
	QKeyEvent select_event(QEvent::KeyPress, Qt::Key_S, Qt::NoModifier);
	QKeyEvent brush_event(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier);
	QKeyEvent grow_event(QEvent::KeyPress, Qt::Key_BracketRight, Qt::NoModifier);
	const int old_size = panel.brush_size_spin->value();
	const bool shortcuts_ok = panel.handleShortcut(&line_event) && panel.currentTool() == VoxelToolType::Line &&
		panel.handleShortcut(&fill_event) && panel.currentTool() == VoxelToolType::Fill &&
		panel.handleShortcut(&select_event) && panel.currentTool() == VoxelToolType::Select &&
		panel.handleShortcut(&brush_event) && panel.handleShortcut(&grow_event) &&
		panel.currentTool() == VoxelToolType::Brush && panel.brush_size_spin->value() == old_size + 1;

	panel.procedural_size_x_spin->setValue(7);
	panel.procedural_size_y_spin->setValue(11);
	panel.procedural_size_z_spin->setValue(5);
	panel.procedural_origin_x_spin->setValue(-3);
	panel.procedural_origin_y_spin->setValue(-5);
	panel.procedural_origin_z_spin->setValue(2);
	panel.procedural_hollow_checkbox->setChecked(true);
	const VoxelProceduralParams panel_params = panel.proceduralParams(VoxelProceduralType::Rock);
	const VoxelProceduralMetrics panel_metrics = VoxelProceduralGenerator::computeMetrics(panel_params);
	const bool procedural_controls_ok = panel_params.size_x == 7 && panel_params.size_y == 11 && panel_params.size_z == 5 &&
		panel_params.origin == Vec3<int>(-3, -5, 2) && panel_params.hollow &&
		panel_metrics.footprint_area == 77 && panel_metrics.footprint_perimeter == 36 &&
		panel_metrics.bounding_volume == 385 && panel_metrics.bounding_surface_area == 334;
	bool lucide_icons_ok = !smoke_icon_dir.isEmpty() && !panel.rebuild_mesh_button->icon().isNull() &&
		!panel.generate_rock_button->icon().isNull() && !panel.selection_move_button->icon().isNull();
	for(QAbstractButton* button : panel.tool_button_group->buttons())
		lucide_icons_ok = lucide_icons_ok && !button->icon().isNull();

	bool mesher_ok = false;
	try
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 0));
		js::Vector<bool, 16> transparent;
		const Reference<Indigo::Mesh> greedy = VoxelMeshBuilding::makeIndigoMeshForVoxelGroup(group, 1, transparent, NULL, VoxelMeshMode::Greedy);
		const Reference<Indigo::Mesh> cubes = VoxelMeshBuilding::makeIndigoMeshForVoxelGroup(group, 1, transparent, NULL, VoxelMeshMode::Cubes);
		mesher_ok = greedy->triangles.size() == 12 && cubes->triangles.size() == 20;
	}
	catch(...)
	{
		mesher_ok = false;
	}

	const bool ok = data_ok && tools_ok && undo_ok && procedural_ok && panel_roundtrip_ok && layer_delete_persisted && legacy_content_preserved &&
		base_opacity_preserved && shortcuts_ok && procedural_controls_ok && lucide_icons_ok && mesher_ok;
	std::ostringstream report;
	report << "{\n";
	report << "  \"ok\": " << (ok ? "true" : "false") << ",\n";
	report << "  \"data\": \"" << data_details << "\",\n";
	report << "  \"tools\": \"" << tool_details << "\",\n";
	report << "  \"undo\": \"" << undo_details << "\",\n";
	report << "  \"procedural\": \"" << procedural_details << "\",\n";
	report << "  \"panel_roundtrip\": " << (panel_roundtrip_ok ? "true" : "false") << ",\n";
	report << "  \"layer_delete_persisted\": " << (layer_delete_persisted ? "true" : "false") << ",\n";
	report << "  \"legacy_content_preserved\": " << (legacy_content_preserved ? "true" : "false") << ",\n";
	report << "  \"base_opacity_preserved\": " << (base_opacity_preserved ? "true" : "false") << ",\n";
	report << "  \"shortcuts\": " << (shortcuts_ok ? "true" : "false") << ",\n";
	report << "  \"procedural_controls_and_metrics\": " << (procedural_controls_ok ? "true" : "false") << ",\n";
	report << "  \"lucide_runtime_icons\": " << (lucide_icons_ok ? "true" : "false") << ",\n";
	report << "  \"mesher_greedy_and_cubes\": " << (mesher_ok ? "true" : "false") << ",\n";
	report << "  \"apply_error\": \"" << apply_error << "\",\n";
	report << "  \"layer_delete_error\": \"" << layer_delete_error << "\",\n";
	report << "  \"legacy_error\": \"" << legacy_error << "\",\n";
	report << "  \"opacity_error\": \"" << opacity_error << "\"\n";
	report << "}\n";
	report_out = report.str();
	return ok;
}


void VoxelEditorPanel::toolButtonClicked(const int id)
{
	selectTool(static_cast<VoxelToolType>(id), true);
}


void VoxelEditorPanel::toolControlChanged()
{
	if(!updating)
		emit toolStateChanged();
}


void VoxelEditorPanel::proceduralControlChanged()
{
	if(!updating)
		refreshProceduralMetrics();
}


void VoxelEditorPanel::randomiseProceduralSeed()
{
	procedural_seed_spin->setValue(static_cast<int>(QRandomGenerator::global()->generate() & 0x7FFFFFFFu));
}


void VoxelEditorPanel::chooseColour()
{
	if(!editable || !activeLayerEditable())
		return;
	const QColor colour = QColorDialog::getColor(toQColor(editor_state.palette.current_colour), this, tr("Voxel colour"));
	if(colour.isValid())
		setCurrentColour(fromQColor(colour), true);
}


void VoxelEditorPanel::paletteColourClicked()
{
	if(!editable || !activeLayerEditable())
		return;
	QPushButton* button = qobject_cast<QPushButton*>(sender());
	if(button)
		setCurrentColour(static_cast<uint32_t>(button->property("voxel_rgba").toULongLong()), true);
}


void VoxelEditorPanel::layerSelectionChanged(const int row)
{
	if(updating || row < 0 || row >= static_cast<int>(editor_state.layers.size()))
		return;
	editor_state.active_layer = row;
	const VoxelLayer& layer = editor_state.layers[row];
	if(!layer.material_indices.empty() && layer.material_indices[0] >= 0 && layer.material_indices[0] < static_cast<int>(material_colours.size()))
	{
		editor_state.current_material_index = layer.material_indices[0];
		editor_state.palette.current_colour = material_colours[editor_state.current_material_index];
		pending_colour_material = false;
	}
	else
		pending_colour_material = true;
	refreshActiveLayerControls();
	refreshColourControls();
	emitMetadataChange();
	emit toolStateChanged();
}


void VoxelEditorPanel::layerNameEdited()
{
	if(updating || !editable)
		return;
	VoxelLayer* layer = VoxelEditorData::activeLayer(editor_state);
	if(!layer || layer->locked)
		return;
	layer->name = layer_name_edit->text().trimmed().toUtf8().constData();
	VoxelEditorData::clamp(editor_state);
	refreshLayerList();
	emitMetadataChange();
}


void VoxelEditorPanel::layerVisibleChanged(const bool checked)
{
	if(updating || !editable)
		return;
	VoxelLayer* layer = VoxelEditorData::activeLayer(editor_state);
	if(!layer || layer->visible == checked)
		return;
	layer->visible = checked;
	layer_render_dirty = true;
	refreshLayerList();
	refreshToolControls();
	emitMeshChange();
}


void VoxelEditorPanel::layerLockedChanged(const bool checked)
{
	if(updating || !editable)
		return;
	VoxelLayer* layer = VoxelEditorData::activeLayer(editor_state);
	if(!layer || layer->locked == checked)
		return;
	layer->locked = checked;
	refreshLayerList();
	refreshActiveLayerControls();
	emitMetadataChange();
}


void VoxelEditorPanel::layerOpacityChanged(const double value)
{
	if(updating || !editable)
		return;
	VoxelLayer* layer = VoxelEditorData::activeLayer(editor_state);
	if(!layer || layer->locked || std::fabs(layer->opacity - static_cast<float>(value)) < 0.0001f)
		return;
	layer->opacity = static_cast<float>(value);
	layer_render_dirty = true;
	emitMeshChange();
}


void VoxelEditorPanel::addLayer()
{
	if(!editable || editor_state.layers.size() >= static_cast<size_t>(VoxelEditorData::MAX_LAYERS))
		return;
	VoxelLayer layer;
	layer.name = "Layer " + std::to_string(editor_state.layers.size() + 1);
	editor_state.layers.push_back(layer);
	editor_state.active_layer = static_cast<int>(editor_state.layers.size()) - 1;
	pending_colour_material = true;
	refreshLayerList();
	refreshActiveLayerControls();
	emitMetadataChange();
	emit toolStateChanged();
}


void VoxelEditorPanel::deleteLayer()
{
	if(!editable || editor_state.layers.size() <= 1)
		return;
	const int index = editor_state.active_layer;
	if(index < 0 || index >= static_cast<int>(editor_state.layers.size()) || editor_state.layers[index].locked)
		return;
	for(const int material_index : editor_state.layers[index].material_indices)
		if(!containsInt(removed_material_indices, material_index))
			removed_material_indices.push_back(material_index);
	editor_state.layers.erase(editor_state.layers.begin() + index);
	editor_state.active_layer = std::min(index, static_cast<int>(editor_state.layers.size()) - 1);
	VoxelEditorData::clamp(editor_state);
	const VoxelLayer* layer = VoxelEditorData::activeLayer(editor_state);
	pending_colour_material = !layer || layer->material_indices.empty();
	refreshAllControls();
	emitMeshChange();
	emit toolStateChanged();
}


void VoxelEditorPanel::moveLayerUp()
{
	const int index = editor_state.active_layer;
	if(!editable || index <= 0 || index >= static_cast<int>(editor_state.layers.size()) || editor_state.layers[index].locked)
		return;
	std::swap(editor_state.layers[index], editor_state.layers[index - 1]);
	editor_state.active_layer = index - 1;
	refreshLayerList();
	emitMetadataChange();
}


void VoxelEditorPanel::moveLayerDown()
{
	const int index = editor_state.active_layer;
	if(!editable || index < 0 || index + 1 >= static_cast<int>(editor_state.layers.size()) || editor_state.layers[index].locked)
		return;
	std::swap(editor_state.layers[index], editor_state.layers[index + 1]);
	editor_state.active_layer = index + 1;
	refreshLayerList();
	emitMetadataChange();
}


void VoxelEditorPanel::renderModeChanged(const int index)
{
	if(updating || !editable || index < 0)
		return;
	const VoxelRenderMode mode = static_cast<VoxelRenderMode>(render_mode_combo->itemData(index).toInt());
	if(mode == VoxelRenderMode::MarchingCubes || editor_state.render_mode == mode)
		return;
	editor_state.render_mode = mode;
	emitMeshChange();
}


void VoxelEditorPanel::rebuildMesh()
{
	if(!updating && editable)
		emit meshRebuildRequested();
}


void VoxelEditorPanel::selectTool(const VoxelToolType tool, const bool emit_change)
{
	selected_tool = tool;
	updating = true;
	if(QAbstractButton* button = tool_button_group->button(static_cast<int>(tool)))
		button->setChecked(true);
	updating = false;
	refreshToolControls();
	if(emit_change)
		emit toolStateChanged();
}


void VoxelEditorPanel::setCurrentColour(const uint32_t rgba, const bool emit_change)
{
	const uint32_t colour = opaqueRGB(rgba);
	editor_state.palette.current_colour = colour;
	const int existing_material = findMaterialForColourInActiveLayer(colour);
	if(existing_material >= 0)
	{
		editor_state.current_material_index = existing_material;
		pending_colour_material = false;
	}
	else
		pending_colour_material = true;

	if(std::find(editor_state.palette.colours.begin(), editor_state.palette.colours.end(), colour) == editor_state.palette.colours.end() &&
		editor_state.palette.colours.size() < static_cast<size_t>(VoxelEditorData::MAX_PALETTE_COLOURS))
		editor_state.palette.colours.push_back(colour);
	editor_state.palette.recent_colours.erase(
		std::remove(editor_state.palette.recent_colours.begin(), editor_state.palette.recent_colours.end(), colour),
		editor_state.palette.recent_colours.end());
	editor_state.palette.recent_colours.insert(editor_state.palette.recent_colours.begin(), colour);
	if(editor_state.palette.recent_colours.size() > 12)
		editor_state.palette.recent_colours.resize(12);
	refreshColourControls();
	if(emit_change)
	{
		emitMetadataChange();
		emit toolStateChanged();
	}
}


void VoxelEditorPanel::refreshAllControls()
{
	refreshToolControls();
	refreshColourControls();
	refreshLayerList();
	refreshActiveLayerControls();
	refreshRenderControls();
}


void VoxelEditorPanel::refreshToolControls()
{
	updating = true;
	if(QAbstractButton* button = tool_button_group->button(static_cast<int>(selected_tool)))
		button->setChecked(true);
	const bool can_edit_layer = activeLayerEditable();
	const bool uses_brush_settings = selected_tool != VoxelToolType::Picker && selected_tool != VoxelToolType::Select && selected_tool != VoxelToolType::Fill;
	tool_settings_widget->setEnabled(editable && can_edit_layer && uses_brush_settings);
	scene_tools_checkbox->setEnabled(editable && (selected_tool == VoxelToolType::Picker || selected_tool == VoxelToolType::Select || can_edit_layer));
	current_colour_button->setEnabled(editable && can_edit_layer);
	selection_copy_button->setEnabled(editable);
	selection_clear_button->setEnabled(editable);
	selection_paste_button->setEnabled(editable && can_edit_layer);
	selection_delete_button->setEnabled(editable && can_edit_layer);
	selection_duplicate_button->setEnabled(editable && can_edit_layer);
	selection_move_button->setEnabled(editable && can_edit_layer);
	for(QPushButton* button : { generate_box_button, generate_ellipsoid_button, generate_rock_button,
		generate_terrain_button, generate_noise_button, generate_crystal_button, generate_wall_button })
		button->setEnabled(editable && can_edit_layer);
	updating = false;
}


void VoxelEditorPanel::refreshColourControls()
{
	const QColor colour = toQColor(editor_state.palette.current_colour);
	current_colour_button->setText(colour.name(QColor::HexRgb).toUpper());
	current_colour_button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; color: %2; font-weight: bold; }")
		.arg(colour.name(QColor::HexRgb), colour.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black")));
	refreshPaletteGrid();
}


void VoxelEditorPanel::refreshPaletteGrid()
{
	clearGrid(palette_grid);
	clearGrid(recent_grid);
	const auto add_buttons = [this](QGridLayout* grid, const std::vector<uint32_t>& colours)
	{
		for(size_t i=0; i<colours.size(); ++i)
		{
			const QColor colour = toQColor(colours[i]);
			QPushButton* button = new QPushButton();
			button->setFixedSize(24, 24);
			button->setProperty("voxel_rgba", QVariant::fromValue<qulonglong>(colours[i]));
			button->setToolTip(colour.name(QColor::HexRgb).toUpper());
			button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; border: 1px solid #606060; }").arg(colour.name(QColor::HexRgb)));
			connect(button, SIGNAL(clicked()), this, SLOT(paletteColourClicked()));
			grid->addWidget(button, static_cast<int>(i / 8), static_cast<int>(i % 8));
		}
	};
	add_buttons(palette_grid, editor_state.palette.colours);
	add_buttons(recent_grid, editor_state.palette.recent_colours);
	recent_label->setVisible(!editor_state.palette.recent_colours.empty());
	if(recent_grid->parentWidget())
		recent_grid->parentWidget()->setVisible(!editor_state.palette.recent_colours.empty());
}


void VoxelEditorPanel::refreshLayerList()
{
	updating = true;
	layer_list->clear();
	for(const VoxelLayer& layer : editor_state.layers)
		layer_list->addItem(layerDisplayName(layer));
	layer_list->setCurrentRow(editor_state.active_layer);
	const VoxelLayer* active_layer = VoxelEditorData::activeLayer(editor_state);
	const bool active_locked = active_layer && active_layer->locked;
	add_layer_button->setEnabled(editable && editor_state.layers.size() < static_cast<size_t>(VoxelEditorData::MAX_LAYERS));
	delete_layer_button->setEnabled(editable && !active_locked && editor_state.layers.size() > 1);
	move_layer_up_button->setEnabled(editable && !active_locked && editor_state.active_layer > 0);
	move_layer_down_button->setEnabled(editable && !active_locked && editor_state.active_layer + 1 < static_cast<int>(editor_state.layers.size()));
	updating = false;
}


void VoxelEditorPanel::refreshActiveLayerControls()
{
	updating = true;
	const VoxelLayer* layer = VoxelEditorData::activeLayer(editor_state);
	const bool has_layer = layer != NULL;
	layer_name_edit->setEnabled(editable && has_layer && !layer->locked);
	layer_visible_checkbox->setEnabled(editable && has_layer);
	layer_locked_checkbox->setEnabled(editable && has_layer);
	layer_opacity_spin->setEnabled(editable && has_layer && !layer->locked);
	if(layer)
	{
		layer_name_edit->setText(QString::fromUtf8(layer->name.c_str()));
		layer_visible_checkbox->setChecked(layer->visible);
		layer_locked_checkbox->setChecked(layer->locked);
		layer_opacity_spin->setValue(layer->opacity);
	}
	updating = false;
	refreshToolControls();
}


void VoxelEditorPanel::refreshRenderControls()
{
	updating = true;
	const int index = render_mode_combo->findData(static_cast<int>(editor_state.render_mode));
	if(index >= 0)
		render_mode_combo->setCurrentIndex(index);
	smooth_normals_checkbox->setChecked(editor_state.smooth_normals);
	surface_threshold_spin->setValue(editor_state.surface_threshold);
	updating = false;
}


void VoxelEditorPanel::refreshProceduralMetrics()
{
	if(!procedural_metrics_label)
		return;
	const VoxelProceduralMetrics metrics = VoxelProceduralGenerator::computeMetrics(proceduralParams(VoxelProceduralType::Box));
	procedural_metrics_label->setText(tr("Footprint: %1 vox²  •  Perimeter: %2 vox  •  Volume: %3 vox³  •  Surface: %4 vox²")
		.arg(static_cast<qulonglong>(metrics.footprint_area))
		.arg(static_cast<qulonglong>(metrics.footprint_perimeter))
		.arg(static_cast<qulonglong>(metrics.bounding_volume))
		.arg(static_cast<qulonglong>(metrics.bounding_surface_area)));
}


void VoxelEditorPanel::applyIcons()
{
	if(icon_directory.isEmpty())
		return;

	const QPalette icon_palette = palette();
	const QColor foreground = icon_palette.color(QPalette::ButtonText);
	const auto themed = [&icon_palette](const QColor& colour)
	{
		return LucideIconUtils::themeAwareColour(colour, icon_palette, QPalette::ButtonText, QPalette::Button);
	};

	for(QAbstractButton* button : tool_button_group->buttons())
	{
		const VoxelToolType tool = static_cast<VoxelToolType>(tool_button_group->id(button));
		const char* name = "boxes";
		QColor colour = foreground;
		switch(tool)
		{
		case VoxelToolType::Brush:  name = "brush"; colour = QColor(QStringLiteral("#60A5FA")); break;
		case VoxelToolType::Eraser: name = "eraser"; colour = QColor(QStringLiteral("#FB7185")); break;
		case VoxelToolType::Paint:  name = "paintbrush"; colour = QColor(QStringLiteral("#C084FC")); break;
		case VoxelToolType::Line:   name = "move-diagonal-2"; colour = QColor(QStringLiteral("#22D3EE")); break;
		case VoxelToolType::Box:    name = "cuboid"; colour = QColor(QStringLiteral("#F97316")); break;
		case VoxelToolType::Sphere: name = "circle"; colour = QColor(QStringLiteral("#4ADE80")); break;
		case VoxelToolType::Fill:   name = "paint-bucket"; colour = QColor(QStringLiteral("#818CF8")); break;
		case VoxelToolType::Picker: name = "pipette"; colour = QColor(QStringLiteral("#FACC15")); break;
		case VoxelToolType::Select: name = "square-dashed-mouse-pointer"; break;
		}
		LucideIconUtils::setButtonIcon(button, icon_directory, QString::fromLatin1(name), themed(colour));
	}

	LucideIconUtils::setButtonIcon(add_layer_button, icon_directory, QStringLiteral("layers-plus"), themed(QColor(QStringLiteral("#4ADE80"))));
	LucideIconUtils::setButtonIcon(delete_layer_button, icon_directory, QStringLiteral("layers-minus"), themed(QColor(QStringLiteral("#FB7185"))));
	LucideIconUtils::setButtonIcon(move_layer_up_button, icon_directory, QStringLiteral("arrow-up"), foreground);
	LucideIconUtils::setButtonIcon(move_layer_down_button, icon_directory, QStringLiteral("arrow-down"), foreground);
	LucideIconUtils::setButtonIcon(rebuild_mesh_button, icon_directory, QStringLiteral("refresh-cw"), themed(QColor(QStringLiteral("#60A5FA"))));

	LucideIconUtils::setButtonIcon(selection_copy_button, icon_directory, QStringLiteral("copy"), foreground);
	LucideIconUtils::setButtonIcon(selection_paste_button, icon_directory, QStringLiteral("clipboard-paste"), themed(QColor(QStringLiteral("#60A5FA"))));
	LucideIconUtils::setButtonIcon(selection_delete_button, icon_directory, QStringLiteral("trash-2"), themed(QColor(QStringLiteral("#FB7185"))));
	LucideIconUtils::setButtonIcon(selection_duplicate_button, icon_directory, QStringLiteral("copy-plus"), themed(QColor(QStringLiteral("#A78BFA"))));
	LucideIconUtils::setButtonIcon(selection_move_button, icon_directory, QStringLiteral("move-3d"), themed(QColor(QStringLiteral("#22D3EE"))));
	LucideIconUtils::setButtonIcon(selection_clear_button, icon_directory, QStringLiteral("square-dashed-mouse-pointer"), foreground);

	LucideIconUtils::setButtonIcon(procedural_random_seed_button, icon_directory, QStringLiteral("dices"), themed(QColor(QStringLiteral("#FACC15"))));
	LucideIconUtils::setButtonIcon(generate_box_button, icon_directory, QStringLiteral("cuboid"), themed(QColor(QStringLiteral("#F97316"))));
	LucideIconUtils::setButtonIcon(generate_ellipsoid_button, icon_directory, QStringLiteral("circle"), themed(QColor(QStringLiteral("#4ADE80"))));
	LucideIconUtils::setButtonIcon(generate_rock_button, icon_directory, QStringLiteral("mountain"), themed(QColor(QStringLiteral("#A8A29E"))));
	LucideIconUtils::setButtonIcon(generate_terrain_button, icon_directory, QStringLiteral("land-plot"), themed(QColor(QStringLiteral("#84CC16"))));
	LucideIconUtils::setButtonIcon(generate_noise_button, icon_directory, QStringLiteral("dices"), themed(QColor(QStringLiteral("#C084FC"))));
	LucideIconUtils::setButtonIcon(generate_crystal_button, icon_directory, QStringLiteral("gem"), themed(QColor(QStringLiteral("#22D3EE"))));
	LucideIconUtils::setButtonIcon(generate_wall_button, icon_directory, QStringLiteral("brick-wall"), themed(QColor(QStringLiteral("#D6B98C"))));

	const char* exchange_icons[] = { "file-input", "file-output", "file-output", "save" };
	for(size_t i=0; i<exchange_buttons.size() && i<4; ++i)
		LucideIconUtils::setButtonIcon(exchange_buttons[i], icon_directory, QString::fromLatin1(exchange_icons[i]), foreground);
}


void VoxelEditorPanel::updateVoxelCount(const size_t count)
{
	voxel_count_label->setText(tr("Voxels: %1").arg(static_cast<qulonglong>(count)));
}


void VoxelEditorPanel::rebuildMaterialColourCache(const WorldObject& object)
{
	material_colours.clear();
	material_colours.reserve(object.materials.size());
	for(const WorldMaterialRef& material : object.materials)
		material_colours.push_back(material.nonNull() ? materialRGB(*material) : 0xFFFFFFFFu);
}


int VoxelEditorPanel::findMaterialForColourInActiveLayer(const uint32_t rgba) const
{
	const VoxelLayer* layer = VoxelEditorData::activeLayer(editor_state);
	if(!layer)
		return -1;
	const uint32_t colour = opaqueRGB(rgba);
	for(const int material_index : layer->material_indices)
		if(material_index >= 0 && material_index < static_cast<int>(material_colours.size()) && material_colours[material_index] == colour)
			return material_index;
	return -1;
}


bool VoxelEditorPanel::activeLayerEditable() const
{
	const VoxelLayer* layer = VoxelEditorData::activeLayer(editor_state);
	return layer && layer->visible && !layer->locked;
}


void VoxelEditorPanel::emitMetadataChange()
{
	if(!updating && editable)
	{
		editor_metadata_dirty = true;
		emit objectMetadataChanged();
	}
}


void VoxelEditorPanel::emitMeshChange()
{
	if(!updating && editable)
	{
		editor_metadata_dirty = true;
		emit meshRebuildRequested();
	}
}
