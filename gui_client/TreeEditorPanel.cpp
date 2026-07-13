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
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
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
	seed_spin = addIntSpin(general_form, tr("Seed"), 1, 2147483647);
	height_spin = addDoubleSpin(general_form, tr("Height"), 0.5, 60.0, 0.1, 2);
	scale_spin = addDoubleSpin(general_form, tr("Scale"), 0.05, 20.0, 0.05, 2);
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
	layout->addWidget(trunk_box);

	QGroupBox* branch_box = new QGroupBox(tr("Branches"), this);
	QFormLayout* branch_form = new QFormLayout(branch_box);
	branch_levels_spin = addIntSpin(branch_form, tr("Branch Levels"), 0, 5);
	branches_per_level_spin = addIntSpin(branch_form, tr("Branches Per Level"), 0, 16);
	branch_angle_spin = addDoubleSpin(branch_form, tr("Branch Angle"), 0.0, 120.0, 1.0, 1);
	branch_length_spin = addDoubleSpin(branch_form, tr("Branch Length"), 0.05, 30.0, 0.1, 2);
	branch_radius_spin = addDoubleSpin(branch_form, tr("Branch Radius"), 0.01, 3.0, 0.01, 2);
	branch_randomness_spin = addDoubleSpin(branch_form, tr("Branch Randomness"), 0.0, 2.0, 0.02, 2);
	branch_start_height_spin = addDoubleSpin(branch_form, tr("Branch Start Height"), 0.0, 0.95, 0.01, 2);
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
	leaf_size_spin = addDoubleSpin(leaves_form, tr("Leaf Size"), 0.02, 10.0, 0.02, 2);
	leaf_alpha_spin = addDoubleSpin(leaves_form, tr("Leaf Alpha"), 0.0, 1.0, 0.02, 2);
	billboard_combo = new QComboBox(this);
	billboard_combo->addItem(tr("Single"), QVariant((int)TreeBillboardMode::Single));
	billboard_combo->addItem(tr("Double Cross"), QVariant((int)TreeBillboardMode::DoubleCross));
	billboard_combo->addItem(tr("Mesh Leaves later"), QVariant((int)TreeBillboardMode::MeshLeaves));
	leaves_form->addRow(tr("Billboard Mode"), billboard_combo);
	connect(billboard_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(controlChanged()));
	layout->addWidget(leaves_box);

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
	p.seed = (uint32_t)seed_spin->value();
	p.height = (float)height_spin->value();
	p.scale = (float)scale_spin->value();
	p.trunkHeight = (float)trunk_height_spin->value();
	p.trunkRadius = (float)trunk_radius_spin->value();
	p.trunkTaper = (float)trunk_taper_spin->value();
	p.trunkCurve = (float)trunk_curve_spin->value();
	p.trunkTwist = (float)trunk_twist_spin->value();
	p.trunkSegments = trunk_segments_spin->value();
	p.trunkSections = trunk_sections_spin->value();
	p.branchLevels = branch_levels_spin->value();
	p.branchesPerLevel = branches_per_level_spin->value();
	p.branchAngle = (float)branch_angle_spin->value();
	p.branchLength = (float)branch_length_spin->value();
	p.branchRadius = (float)branch_radius_spin->value();
	p.branchRandomness = (float)branch_randomness_spin->value();
	p.branchStartHeight = (float)branch_start_height_spin->value();
	p.leafType = (TreeLeafType)leaf_type_combo->currentData().toInt();
	p.leafCount = leaf_count_spin->value();
	p.leafSize = (float)leaf_size_spin->value();
	p.leafAlpha = (float)leaf_alpha_spin->value();
	p.billboardMode = (TreeBillboardMode)billboard_combo->currentData().toInt();
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
	seed_spin->setValue((int)std::min<uint32_t>(params.seed, 2147483647u));
	height_spin->setValue(params.height);
	scale_spin->setValue(params.scale);
	trunk_height_spin->setValue(params.trunkHeight);
	trunk_radius_spin->setValue(params.trunkRadius);
	trunk_taper_spin->setValue(params.trunkTaper);
	trunk_curve_spin->setValue(params.trunkCurve);
	trunk_twist_spin->setValue(params.trunkTwist);
	trunk_segments_spin->setValue(params.trunkSegments);
	trunk_sections_spin->setValue(params.trunkSections);
	branch_levels_spin->setValue(params.branchLevels);
	branches_per_level_spin->setValue(params.branchesPerLevel);
	branch_angle_spin->setValue(params.branchAngle);
	branch_length_spin->setValue(params.branchLength);
	branch_radius_spin->setValue(params.branchRadius);
	branch_randomness_spin->setValue(params.branchRandomness);
	branch_start_height_spin->setValue(params.branchStartHeight);
	set_combo(leaf_type_combo, (int)params.leafType);
	leaf_count_spin->setValue(params.leafCount);
	leaf_size_spin->setValue(params.leafSize);
	leaf_alpha_spin->setValue(params.leafAlpha);
	set_combo(billboard_combo, (int)params.billboardMode);
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

