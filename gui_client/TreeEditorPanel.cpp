/*=====================================================================
TreeEditorPanel.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "TreeEditorPanel.h"
#include "TreeObject.h"
#include "TreePresets.h"
#include "TreeSerialization.h"


#include "../shared/WorldObject.h"
#include <QtCore/QRandomGenerator>
#include <QtCore/QTimer>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>


TreeEditorPanel::TreeEditorPanel(QWidget* parent)
:	QWidget(parent),
	settings(NULL),
	updating(false),
	current_params(TreeSerialization::defaultParams())
{
	rebuild_timer = new QTimer(this);
	rebuild_timer->setSingleShot(true);
	rebuild_timer->setInterval(180);
	connect(rebuild_timer, SIGNAL(timeout()), this, SLOT(rebuildNow()));
	createUi();
}


void TreeEditorPanel::init(QSettings* settings_)
{
	settings = settings_;
	(void)settings;
}


QDoubleSpinBox* TreeEditorPanel::addDoubleSpin(QFormLayout* form, const QString& label, double min_v, double max_v, double step, int decimals)
{
	QDoubleSpinBox* spin = new QDoubleSpinBox(this);
	spin->setRange(min_v, max_v);
	spin->setSingleStep(step);
	spin->setDecimals(decimals);
	form->addRow(label, spin);
	connect(spin, SIGNAL(valueChanged(double)), this, SLOT(controlChanged()));
	return spin;
}


QSpinBox* TreeEditorPanel::addIntSpin(QFormLayout* form, const QString& label, int min_v, int max_v)
{
	QSpinBox* spin = new QSpinBox(this);
	spin->setRange(min_v, max_v);
	form->addRow(label, spin);
	connect(spin, SIGNAL(valueChanged(int)), this, SLOT(controlChanged()));
	return spin;
}


void TreeEditorPanel::createUi()
{
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	mode_tabs = new QTabWidget(this);
	mode_tabs->addTab(new QWidget(this), tr("Tree"));
	mode_tabs->addTab(new QWidget(this), tr("Export"));
	mode_tabs->setTabEnabled(1, false);
	layout->addWidget(mode_tabs);

	info_label = new QLabel(tr("Tree Editor"), this);
	info_label->setWordWrap(true);
	layout->addWidget(info_label);

	QGroupBox* presets_box = new QGroupBox(tr("Presets"), this);
	QFormLayout* presets_form = new QFormLayout(presets_box);
	preset_combo = new QComboBox(this);
	preset_combo->addItem(tr("Oak"), QVariant((int)TreePresetType::Oak));
	preset_combo->addItem(tr("Birch"), QVariant((int)TreePresetType::Birch));
	preset_combo->addItem(tr("Pine"), QVariant((int)TreePresetType::Pine));
	preset_combo->addItem(tr("Dead Tree"), QVariant((int)TreePresetType::DeadTree));
	preset_combo->addItem(tr("Bush"), QVariant((int)TreePresetType::Bush));
	preset_combo->addItem(tr("Custom"), QVariant((int)TreePresetType::Custom));
	presets_form->addRow(tr("Preset"), preset_combo);
	connect(preset_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(presetChanged(int)));
	layout->addWidget(presets_box);

	QGroupBox* general_box = new QGroupBox(tr("General"), this);
	QFormLayout* general_form = new QFormLayout(general_box);
	tree_type_combo = new QComboBox(this);
	tree_type_combo->addItem(tr("Deciduous"), QVariant((int)TreeType::Deciduous));
	tree_type_combo->addItem(tr("Evergreen"), QVariant((int)TreeType::Evergreen));
	general_form->addRow(tr("Tree Type"), tree_type_combo);
	connect(tree_type_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(controlChanged()));
	seed_spin = addIntSpin(general_form, tr("Seed"), 1, 2147483647);
	height_spin = addDoubleSpin(general_form, tr("Height"), 0.5, 60.0, 0.1, 2);
	layout->addWidget(general_box);

	QGroupBox* trunk_box = new QGroupBox(tr("Trunk"), this);
	QFormLayout* trunk_form = new QFormLayout(trunk_box);
	trunk_height_spin = addDoubleSpin(trunk_form, tr("Trunk Height"), 0.25, 60.0, 0.1, 2);
	trunk_radius_spin = addDoubleSpin(trunk_form, tr("Trunk Radius"), 0.02, 5.0, 0.02, 2);
	trunk_taper_spin = addDoubleSpin(trunk_form, tr("Trunk Taper"), 0.02, 1.0, 0.02, 2);
	trunk_curve_spin = addDoubleSpin(trunk_form, tr("Trunk Curve"), -2.0, 2.0, 0.02, 2);
	trunk_twist_spin = addDoubleSpin(trunk_form, tr("Trunk Twist"), -3.14, 3.14, 0.02, 2);
	trunk_segments_spin = addIntSpin(trunk_form, tr("Trunk Segments"), 3, 32);
	trunk_sections_spin = addIntSpin(trunk_form, tr("Trunk Sections"), 1, 48);
	bark_red_spin = addDoubleSpin(trunk_form, tr("Bark Red"), 0.0, 1.0, 0.01, 2);
	bark_green_spin = addDoubleSpin(trunk_form, tr("Bark Green"), 0.0, 1.0, 0.01, 2);
	bark_blue_spin = addDoubleSpin(trunk_form, tr("Bark Blue"), 0.0, 1.0, 0.01, 2);
	bark_texture_combo = new QComboBox(this);
	bark_texture_combo->addItem(QStringLiteral("Bark001"), QStringLiteral("Bark001"));
	bark_texture_combo->addItem(QStringLiteral("Bark006"), QStringLiteral("Bark006"));
	trunk_form->addRow(tr("Bark Texture"), bark_texture_combo);
	connect(bark_texture_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(controlChanged()));
	bark_textured_checkbox = new QCheckBox(tr("Bark Textured"), this);
	trunk_form->addRow(QString(), bark_textured_checkbox);
	connect(bark_textured_checkbox, SIGNAL(toggled(bool)), this, SLOT(controlChanged()));
	bark_flat_shading_checkbox = new QCheckBox(tr("Bark Flat Shading"), this);
	trunk_form->addRow(QString(), bark_flat_shading_checkbox);
	connect(bark_flat_shading_checkbox, SIGNAL(toggled(bool)), this, SLOT(controlChanged()));
	bark_texture_scale_x_spin = addDoubleSpin(trunk_form, tr("Bark Texture X Scale"), 0.05, 50.0, 0.1, 2);
	bark_texture_scale_y_spin = addDoubleSpin(trunk_form, tr("Bark Texture Y Scale"), 0.05, 50.0, 0.1, 2);
	layout->addWidget(trunk_box);

	QGroupBox* branch_box = new QGroupBox(tr("Branches"), this);
	QFormLayout* branch_form = new QFormLayout(branch_box);
	branch_levels_spin = addIntSpin(branch_form, tr("Branch Levels"), 0, 5);
	branches_per_level_spin = addIntSpin(branch_form, tr("Branches Per Level"), 0, 16);
	branch_angle_spin = addDoubleSpin(branch_form, tr("Branch Angle"), 0.0, 120.0, 1.0, 1);
	branch_length_spin = addDoubleSpin(branch_form, tr("Branch Length"), 0.05, 30.0, 0.1, 2);
	branch_radius_spin = addDoubleSpin(branch_form, tr("Branch Radius"), 0.01, 3.0, 0.01, 2);
	branch_taper_spin = addDoubleSpin(branch_form, tr("Branch Taper"), 0.02, 1.0, 0.02, 2);
	branch_curve_spin = addDoubleSpin(branch_form, tr("Branch Curve"), -2.0, 2.0, 0.02, 2);
	branch_twist_spin = addDoubleSpin(branch_form, tr("Branch Twist"), -3.14, 3.14, 0.02, 2);
	branch_randomness_spin = addDoubleSpin(branch_form, tr("Branch Randomness"), 0.0, 2.0, 0.02, 2);
	branch_start_height_spin = addDoubleSpin(branch_form, tr("Branch Start Height"), 0.0, 0.95, 0.01, 2);
	branch_force_x_spin = addDoubleSpin(branch_form, tr("Branch Force X"), -1.0, 1.0, 0.01, 2);
	branch_force_y_spin = addDoubleSpin(branch_form, tr("Branch Force Y"), -1.0, 1.0, 0.01, 2);
	branch_force_z_spin = addDoubleSpin(branch_form, tr("Branch Force Z"), -1.0, 1.0, 0.01, 2);
	branch_force_strength_spin = addDoubleSpin(branch_form, tr("Branch Force Strength"), -1.0, 1.0, 0.01, 3);
	branch_gnarliness_spin = addDoubleSpin(branch_form, tr("Branch Gnarliness"), 0.0, 2.0, 0.02, 2);
	layout->addWidget(branch_box);

	QGroupBox* leaves_box = new QGroupBox(tr("Leaves"), this);
	QFormLayout* leaves_form = new QFormLayout(leaves_box);
	leaf_type_combo = new QComboBox(this);
	leaf_type_combo->addItem(tr("Simple"), QVariant((int)TreeLeafType::Simple));
	leaf_type_combo->addItem(tr("Oak"), QVariant((int)TreeLeafType::Oak));
	leaf_type_combo->addItem(tr("Birch"), QVariant((int)TreeLeafType::Birch));
	leaf_type_combo->addItem(tr("Pine Needles"), QVariant((int)TreeLeafType::PineNeedles));
	leaf_type_combo->addItem(tr("None"), QVariant((int)TreeLeafType::None));
	leaves_form->addRow(tr("Leaf Type"), leaf_type_combo);
	connect(leaf_type_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(controlChanged()));
	leaf_count_spin = addIntSpin(leaves_form, tr("Leaf Count"), 0, 2000);
	leaf_angle_spin = addDoubleSpin(leaves_form, tr("Leaf Angle"), -90.0, 90.0, 1.0, 1);
	leaf_size_spin = addDoubleSpin(leaves_form, tr("Leaf Size"), 0.02, 10.0, 0.02, 2);
	leaf_size_randomness_spin = addDoubleSpin(leaves_form, tr("Leaf Size Variance"), 0.0, 1.0, 0.02, 2);
	leaf_red_spin = addDoubleSpin(leaves_form, tr("Leaf Red"), 0.0, 1.0, 0.01, 2);
	leaf_green_spin = addDoubleSpin(leaves_form, tr("Leaf Green"), 0.0, 1.0, 0.01, 2);
	leaf_blue_spin = addDoubleSpin(leaves_form, tr("Leaf Blue"), 0.0, 1.0, 0.01, 2);
	leaf_alpha_spin = addDoubleSpin(leaves_form, tr("Leaf Alpha"), 0.0, 1.0, 0.02, 2);
	leaf_alpha_test_spin = addDoubleSpin(leaves_form, tr("Leaf Alpha Test"), 0.0, 1.0, 0.02, 2);
	leaf_rounded_normals_checkbox = new QCheckBox(tr("Rounded Leaf Normals"), this);
	leaves_form->addRow(QString(), leaf_rounded_normals_checkbox);
	connect(leaf_rounded_normals_checkbox, SIGNAL(toggled(bool)), this, SLOT(controlChanged()));
	leaf_start_level_spin = addIntSpin(leaves_form, tr("Leaf Start Level"), 0, 5);
	billboard_combo = new QComboBox(this);
	billboard_combo->addItem(tr("Single"), QVariant((int)TreeBillboardMode::Single));
	billboard_combo->addItem(tr("Double Cross"), QVariant((int)TreeBillboardMode::DoubleCross));
	billboard_combo->addItem(tr("Mesh Leaves later"), QVariant((int)TreeBillboardMode::MeshLeaves));
	leaves_form->addRow(tr("Billboard Mode"), billboard_combo);
	connect(billboard_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(controlChanged()));
	layout->addWidget(leaves_box);

	QGroupBox* trellis_box = new QGroupBox(tr("Trellis"), this);
	QFormLayout* trellis_form = new QFormLayout(trellis_box);
	trellis_enabled_checkbox = new QCheckBox(tr("Trellis Enabled"), this);
	trellis_form->addRow(QString(), trellis_enabled_checkbox);
	connect(trellis_enabled_checkbox, SIGNAL(toggled(bool)), this, SLOT(controlChanged()));
	trellis_x_spin = addDoubleSpin(trellis_form, tr("Trellis X"), -100.0, 100.0, 0.1, 2);
	trellis_y_spin = addDoubleSpin(trellis_form, tr("Trellis Y"), -100.0, 100.0, 0.1, 2);
	trellis_z_spin = addDoubleSpin(trellis_form, tr("Trellis Z"), -100.0, 100.0, 0.1, 2);
	trellis_width_spin = addDoubleSpin(trellis_form, tr("Trellis Width"), 0.1, 100.0, 0.1, 2);
	trellis_height_spin = addDoubleSpin(trellis_form, tr("Trellis Height"), 0.1, 100.0, 0.1, 2);
	trellis_spacing_spin = addDoubleSpin(trellis_form, tr("Trellis Spacing"), 0.1, 20.0, 0.1, 2);
	trellis_force_strength_spin = addDoubleSpin(trellis_form, tr("Trellis Force"), 0.0, 2.0, 0.01, 3);
	trellis_force_max_distance_spin = addDoubleSpin(trellis_form, tr("Trellis Max Distance"), 0.0, 50.0, 0.1, 2);
	trellis_force_falloff_spin = addDoubleSpin(trellis_form, tr("Trellis Falloff"), 0.1, 8.0, 0.1, 2);
	trellis_cylinder_radius_spin = addDoubleSpin(trellis_form, tr("Trellis Cylinder Radius"), 0.005, 2.0, 0.01, 3);
	trellis_visible_checkbox = new QCheckBox(tr("Trellis Visible"), this);
	trellis_form->addRow(QString(), trellis_visible_checkbox);
	connect(trellis_visible_checkbox, SIGNAL(toggled(bool)), this, SLOT(controlChanged()));
	layout->addWidget(trellis_box);

	QGroupBox* optim_box = new QGroupBox(tr("Optimization"), this);
	QFormLayout* optim_form = new QFormLayout(optim_box);
	quality_combo = new QComboBox(this);
	quality_combo->addItem(tr("Low"), QVariant((int)TreeQuality::Low));
	quality_combo->addItem(tr("Medium"), QVariant((int)TreeQuality::Medium));
	quality_combo->addItem(tr("High"), QVariant((int)TreeQuality::High));
	optim_form->addRow(tr("Quality"), quality_combo);
	connect(quality_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(controlChanged()));
	lod_checkbox = new QCheckBox(tr("LOD Enabled"), this);
	optim_form->addRow(QString(), lod_checkbox);
	connect(lod_checkbox, SIGNAL(toggled(bool)), this, SLOT(controlChanged()));
	collision_combo = new QComboBox(this);
	collision_combo->addItem(tr("None"), QVariant((int)TreeCollisionMode::None));
	collision_combo->addItem(tr("Trunk Only"), QVariant((int)TreeCollisionMode::TrunkOnly));
	collision_combo->addItem(tr("Simplified"), QVariant((int)TreeCollisionMode::Simplified));
	optim_form->addRow(tr("Collision"), collision_combo);
	connect(collision_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(controlChanged()));
	cast_shadows_checkbox = new QCheckBox(tr("Cast Shadows"), this);
	optim_form->addRow(QString(), cast_shadows_checkbox);
	connect(cast_shadows_checkbox, SIGNAL(toggled(bool)), this, SLOT(controlChanged()));
	layout->addWidget(optim_box);

	QGroupBox* actions_box = new QGroupBox(tr("Actions"), this);
	QVBoxLayout* actions_layout = new QVBoxLayout(actions_box);
	regenerate_button = new QPushButton(tr("Regenerate"), this);
	random_seed_button = new QPushButton(tr("Random Seed"), this);
	reset_preset_button = new QPushButton(tr("Reset Preset"), this);
	QPushButton* save_preset_button = new QPushButton(tr("Save Preset later"), this);
	QPushButton* export_button = new QPushButton(tr("Export GLB later"), this);
	delete_button = new QPushButton(tr("Delete Tree"), this);
	save_preset_button->setEnabled(false);
	export_button->setEnabled(false);
	actions_layout->addWidget(regenerate_button);
	actions_layout->addWidget(random_seed_button);
	actions_layout->addWidget(reset_preset_button);
	actions_layout->addWidget(save_preset_button);
	actions_layout->addWidget(export_button);
	actions_layout->addWidget(delete_button);
	connect(regenerate_button, SIGNAL(clicked()), this, SLOT(rebuildNow()));
	connect(random_seed_button, SIGNAL(clicked()), this, SLOT(randomSeed()));
	connect(reset_preset_button, SIGNAL(clicked()), this, SLOT(resetPreset()));
	connect(delete_button, SIGNAL(clicked()), this, SIGNAL(deleteObjectRequested()));
	layout->addWidget(actions_box);
	layout->addStretch(1);

	setControlsFromParams(current_params);
}


void TreeEditorPanel::setFromObject(const WorldObject& ob, bool)
{
	std::string parse_error;
	current_params = TreeSerialization::fromContent(ob.content, &parse_error);
	setControlsFromParams(current_params);
	info_label->setText(parse_error.empty() ? tr("Procedural Tree") : QString::fromStdString(parse_error));
}


void TreeEditorPanel::toObject(WorldObject& ob_out)
{
	current_params = controlsToParams();
	TreeObject::applyToWorldObject(ob_out, current_params, /*rebuild_mesh=*/true);
}


void TreeEditorPanel::setControlsEnabled(bool enabled)
{
	setEnabled(enabled);
}


void TreeEditorPanel::setControlsEditable(bool editable)
{
	setEnabled(editable);
}


void TreeEditorPanel::setTransformFromObject(const WorldObject&)
{
}


void TreeEditorPanel::objectPickedUp()
{
}


void TreeEditorPanel::objectDropped()
{
}


bool TreeEditorPanel::posAndRot3DControlsEnabled() const
{
	return false;
}


bool TreeEditorPanel::snapToGridChecked() const
{
	return false;
}


double TreeEditorPanel::gridSpacing() const
{
	return 1.0;
}


TreeParams TreeEditorPanel::controlsToParams() const
{
	TreeParams p = current_params;
	p.preset = (TreePresetType)preset_combo->currentData().toInt();
	p.type = (TreeType)tree_type_combo->currentData().toInt();
	p.seed = (uint32_t)seed_spin->value();
	p.height = (float)height_spin->value();
	p.scale = 1.0f;
	p.trunkHeight = (float)trunk_height_spin->value();
	p.trunkRadius = (float)trunk_radius_spin->value();
	p.trunkTaper = (float)trunk_taper_spin->value();
	p.trunkCurve = (float)trunk_curve_spin->value();
	p.trunkTwist = (float)trunk_twist_spin->value();
	p.trunkSegments = trunk_segments_spin->value();
	p.trunkSections = trunk_sections_spin->value();
	p.barkColor = {(float)bark_red_spin->value(), (float)bark_green_spin->value(), (float)bark_blue_spin->value(), 1.0f};
	p.barkTextureType = bark_texture_combo->currentData().toString().toStdString();
	p.barkTextured = bark_textured_checkbox->isChecked();
	p.barkFlatShading = bark_flat_shading_checkbox->isChecked();
	p.barkTextureScaleX = (float)bark_texture_scale_x_spin->value();
	p.barkTextureScaleY = (float)bark_texture_scale_y_spin->value();
	p.branchLevels = branch_levels_spin->value();
	p.branchesPerLevel = branches_per_level_spin->value();
	p.branchAngle = (float)branch_angle_spin->value();
	p.branchLength = (float)branch_length_spin->value();
	p.branchRadius = (float)branch_radius_spin->value();
	p.branchTaper = (float)branch_taper_spin->value();
	p.branchCurve = (float)branch_curve_spin->value();
	p.branchTwist = (float)branch_twist_spin->value();
	p.branchRandomness = (float)branch_randomness_spin->value();
	p.branchStartHeight = (float)branch_start_height_spin->value();
	p.branchForceDirection = {(float)branch_force_x_spin->value(), (float)branch_force_y_spin->value(), (float)branch_force_z_spin->value()};
	p.branchForceStrength = (float)branch_force_strength_spin->value();
	p.branchGnarliness = (float)branch_gnarliness_spin->value();
	p.leafType = (TreeLeafType)leaf_type_combo->currentData().toInt();
	p.leafCount = leaf_count_spin->value();
	p.leafAngle = (float)leaf_angle_spin->value();
	p.leafSize = (float)leaf_size_spin->value();
	p.leafSizeRandomness = (float)leaf_size_randomness_spin->value();
	p.leafColor = {(float)leaf_red_spin->value(), (float)leaf_green_spin->value(), (float)leaf_blue_spin->value(), (float)leaf_alpha_spin->value()};
	p.leafAlpha = (float)leaf_alpha_spin->value();
	p.leafAlphaTest = (float)leaf_alpha_test_spin->value();
	p.leafRoundedNormals = leaf_rounded_normals_checkbox->isChecked();
	p.leafStartLevel = leaf_start_level_spin->value();
	p.billboardMode = (TreeBillboardMode)billboard_combo->currentData().toInt();
	p.trellisEnabled = trellis_enabled_checkbox->isChecked();
	p.trellisPosition = {(float)trellis_x_spin->value(), (float)trellis_y_spin->value(), (float)trellis_z_spin->value()};
	p.trellisWidth = (float)trellis_width_spin->value();
	p.trellisHeight = (float)trellis_height_spin->value();
	p.trellisSpacing = (float)trellis_spacing_spin->value();
	p.trellisForceStrength = (float)trellis_force_strength_spin->value();
	p.trellisForceMaxDistance = (float)trellis_force_max_distance_spin->value();
	p.trellisForceFalloff = (float)trellis_force_falloff_spin->value();
	p.trellisCylinderRadius = (float)trellis_cylinder_radius_spin->value();
	p.trellisVisible = trellis_visible_checkbox->isChecked();
	p.quality = (TreeQuality)quality_combo->currentData().toInt();
	p.lodEnabled = lod_checkbox->isChecked();
	p.collisionMode = (TreeCollisionMode)collision_combo->currentData().toInt();
	p.castShadows = cast_shadows_checkbox->isChecked();
	TreeSerialization::clamp(p);
	return p;
}


void TreeEditorPanel::setControlsFromParams(const TreeParams& params)
{
	updating = true;
	const auto set_combo = [](QComboBox* combo, int value)
	{
		const int i = combo->findData(QVariant(value));
		if(i >= 0)
			combo->setCurrentIndex(i);
	};
	set_combo(preset_combo, (int)params.preset);
	set_combo(tree_type_combo, (int)params.type);
	seed_spin->setValue((int)std::min<uint32_t>(params.seed, 2147483647u));
	height_spin->setValue(params.height);
	trunk_height_spin->setValue(params.trunkHeight);
	trunk_radius_spin->setValue(params.trunkRadius);
	trunk_taper_spin->setValue(params.trunkTaper);
	trunk_curve_spin->setValue(params.trunkCurve);
	trunk_twist_spin->setValue(params.trunkTwist);
	trunk_segments_spin->setValue(params.trunkSegments);
	trunk_sections_spin->setValue(params.trunkSections);
	bark_red_spin->setValue(params.barkColor.r);
	bark_green_spin->setValue(params.barkColor.g);
	bark_blue_spin->setValue(params.barkColor.b);
	const int bark_index = bark_texture_combo->findData(QString::fromStdString(params.barkTextureType));
	if(bark_index >= 0)
		bark_texture_combo->setCurrentIndex(bark_index);
	bark_textured_checkbox->setChecked(params.barkTextured);
	bark_flat_shading_checkbox->setChecked(params.barkFlatShading);
	bark_texture_scale_x_spin->setValue(params.barkTextureScaleX);
	bark_texture_scale_y_spin->setValue(params.barkTextureScaleY);
	branch_levels_spin->setValue(params.branchLevels);
	branches_per_level_spin->setValue(params.branchesPerLevel);
	branch_angle_spin->setValue(params.branchAngle);
	branch_length_spin->setValue(params.branchLength);
	branch_radius_spin->setValue(params.branchRadius);
	branch_taper_spin->setValue(params.branchTaper);
	branch_curve_spin->setValue(params.branchCurve);
	branch_twist_spin->setValue(params.branchTwist);
	branch_randomness_spin->setValue(params.branchRandomness);
	branch_start_height_spin->setValue(params.branchStartHeight);
	branch_force_x_spin->setValue(params.branchForceDirection.x);
	branch_force_y_spin->setValue(params.branchForceDirection.y);
	branch_force_z_spin->setValue(params.branchForceDirection.z);
	branch_force_strength_spin->setValue(params.branchForceStrength);
	branch_gnarliness_spin->setValue(params.branchGnarliness);
	set_combo(leaf_type_combo, (int)params.leafType);
	leaf_count_spin->setValue(params.leafCount);
	leaf_angle_spin->setValue(params.leafAngle);
	leaf_size_spin->setValue(params.leafSize);
	leaf_size_randomness_spin->setValue(params.leafSizeRandomness);
	leaf_red_spin->setValue(params.leafColor.r);
	leaf_green_spin->setValue(params.leafColor.g);
	leaf_blue_spin->setValue(params.leafColor.b);
	leaf_alpha_spin->setValue(params.leafAlpha);
	leaf_alpha_test_spin->setValue(params.leafAlphaTest);
	leaf_rounded_normals_checkbox->setChecked(params.leafRoundedNormals);
	leaf_start_level_spin->setValue(params.leafStartLevel);
	set_combo(billboard_combo, (int)params.billboardMode);
	trellis_enabled_checkbox->setChecked(params.trellisEnabled);
	trellis_x_spin->setValue(params.trellisPosition.x);
	trellis_y_spin->setValue(params.trellisPosition.y);
	trellis_z_spin->setValue(params.trellisPosition.z);
	trellis_width_spin->setValue(params.trellisWidth);
	trellis_height_spin->setValue(params.trellisHeight);
	trellis_spacing_spin->setValue(params.trellisSpacing);
	trellis_force_strength_spin->setValue(params.trellisForceStrength);
	trellis_force_max_distance_spin->setValue(params.trellisForceMaxDistance);
	trellis_force_falloff_spin->setValue(params.trellisForceFalloff);
	trellis_cylinder_radius_spin->setValue(params.trellisCylinderRadius);
	trellis_visible_checkbox->setChecked(params.trellisVisible);
	set_combo(quality_combo, (int)params.quality);
	lod_checkbox->setChecked(params.lodEnabled);
	set_combo(collision_combo, (int)params.collisionMode);
	cast_shadows_checkbox->setChecked(params.castShadows);
	updating = false;
}


void TreeEditorPanel::emitObjectChangedDebounced()
{
	if(updating)
		return;
	rebuild_timer->start();
}


void TreeEditorPanel::controlChanged()
{
	current_params = controlsToParams();
	if(current_params.preset != TreePresetType::Custom)
	{
		current_params.preset = TreePresetType::Custom;
		updating = true;
		const int i = preset_combo->findData(QVariant((int)TreePresetType::Custom));
		if(i >= 0)
			preset_combo->setCurrentIndex(i);
		updating = false;
	}
	emitObjectChangedDebounced();
}


void TreeEditorPanel::rebuildNow()
{
	current_params = controlsToParams();
	emit objectChanged();
}


void TreeEditorPanel::randomSeed()
{
	seed_spin->setValue((int)(QRandomGenerator::global()->generate() & 0x7fffffffu));
	rebuildNow();
}


void TreeEditorPanel::presetChanged(int)
{
	if(updating)
		return;
	const TreePresetType preset = (TreePresetType)preset_combo->currentData().toInt();
	if(preset == TreePresetType::Custom)
		return;
	TreeParams p = TreePresets::preset(preset);
	p.seed = current_params.seed;
	current_params = p;
	setControlsFromParams(current_params);
	rebuildNow();
}


void TreeEditorPanel::resetPreset()
{
	TreePresetType preset = (TreePresetType)preset_combo->currentData().toInt();
	if(preset == TreePresetType::Custom)
		preset = current_params.preset == TreePresetType::Custom ? TreePresetType::Oak : current_params.preset;
	current_params = TreePresets::preset(preset);
	setControlsFromParams(current_params);
	rebuildNow();
}
