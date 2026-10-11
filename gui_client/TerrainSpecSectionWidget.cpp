/*=====================================================================
TerrainSpecSectionWidget.cpp
----------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "TerrainSpecSectionWidget.h"


#include "../qt/SignalBlocker.h"
#include <QtCore/QSettings>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QFrame>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QDoubleSpinBox>
#include <QtGui/QPixmap>


TerrainSpecSectionWidget::TerrainSpecSectionWidget(QWidget* parent)
:	QWidget(parent)
{
	setupUi(this);
	QHBoxLayout* map_rows[5] = { heightmapRowLayout, maskMapRowLayout, treeMaskMapRowLayout, roadMaskMapRowLayout, buildingMaskMapRowLayout };
	FileSelectWidget* map_fields[5] = { heightmapURLFileSelectWidget, maskMapURLFileSelectWidget, treeMaskMapURLFileSelectWidget, roadMaskMapURLFileSelectWidget, buildingMaskMapURLFileSelectWidget };
	for(int i=0; i<5; ++i)
	{
		map_preview_labels[i] = new QLabel(tr("No preview"), this);
		map_preview_labels[i]->setObjectName(QString("sectionMapPreview%1").arg(i));
		map_preview_labels[i]->setFixedSize(52, 34);
		map_preview_labels[i]->setAlignment(Qt::AlignCenter);
		map_preview_labels[i]->setFrameShape(QFrame::StyledPanel);
		map_preview_labels[i]->setToolTip(tr("Preview of the assigned section map"));
		map_scale_spin_boxes[i] = new QDoubleSpinBox(this);
		map_scale_spin_boxes[i]->setObjectName(QString("sectionMapScale%1").arg(i));
		map_scale_spin_boxes[i]->setRange(0.01, 100.0);
		map_scale_spin_boxes[i]->setDecimals(2);
		map_scale_spin_boxes[i]->setSingleStep(0.1);
		map_scale_spin_boxes[i]->setValue(1.0);
		map_scale_spin_boxes[i]->setSuffix(QStringLiteral(" ×"));
		map_scale_spin_boxes[i]->setToolTip(tr("Controls how much of this map is used in the section. Higher values zoom in; lower values zoom out."));
		map_scale_labels[i] = new QLabel(tr("Scale"), this);
		map_scale_labels[i]->setBuddy(map_scale_spin_boxes[i]);
		map_rows[i]->insertWidget(1, map_preview_labels[i]);
		map_rows[i]->insertWidget(2, map_scale_labels[i]);
		map_rows[i]->insertWidget(3, map_scale_spin_boxes[i]);
		map_clear_buttons[i] = new QPushButton(this);
		map_rows[i]->addWidget(map_clear_buttons[i]);
		FileSelectWidget* field = map_fields[i];
		connect(map_clear_buttons[i], &QPushButton::clicked, this, [this, field]()
		{
			field->setFilename(QString());
			emit mapClearedSignal();
		});
	}
	retranslateMapClearButtons();

	// Terrain sections sit inside a resizable dock.  Let their map rows wrap
	// rather than imposing the desktop-width size hint on a narrow dock.
	setMinimumWidth(0);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	formLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
	for(QLabel* label : findChildren<QLabel*>())
		label->setWordWrap(true);
	for(QWidget* child : findChildren<QWidget*>())
	{
		if(child != this)
			child->setMinimumWidth(0);
	}

	connect(this->removeTerrainSectionPushButton, SIGNAL(clicked()), this, SIGNAL(removeButtonClickedSignal()));
}


TerrainSpecSectionWidget::~TerrainSpecSectionWidget()
{}


void TerrainSpecSectionWidget::updateControlsEditable(bool editable)
{
	xSpinBox->setReadOnly(!editable);
	ySpinBox->setReadOnly(!editable);
	heightmapURLFileSelectWidget->setReadOnly(!editable);
	maskMapURLFileSelectWidget->setReadOnly(!editable);
	treeMaskMapURLFileSelectWidget->setReadOnly(!editable);
	roadMaskMapURLFileSelectWidget->setReadOnly(!editable);
	buildingMaskMapURLFileSelectWidget->setReadOnly(!editable);
	heightmapEnabledCheckBox->setEnabled(editable);
	maskMapEnabledCheckBox->setEnabled(editable);
	treeMaskMapEnabledCheckBox->setEnabled(editable);
	roadMaskMapEnabledCheckBox->setEnabled(editable);
	buildingMaskMapEnabledCheckBox->setEnabled(editable);
	removeTerrainSectionPushButton->setEnabled(editable);
	for(int i=0; i<5; ++i) map_clear_buttons[i]->setEnabled(editable);
	for(int i=0; i<5; ++i) map_scale_spin_boxes[i]->setEnabled(editable);
}

void TerrainSpecSectionWidget::retranslateMapClearButtons()
{
	const QString map_names[5] = {
		tr("Height map"), tr("Surface material mask"), tr("Tree mask"), tr("Road reference mask"), tr("Building reference mask")
	};
	const QString scale_guidance[5] = {
		tr("Assign the height image in World > Terrain section maps > Height map. This controls terrain elevation. Scale changes the sampled area around the image centre."),
		tr("Assign the RGBA weight image in World > Terrain section maps > Surface material mask. Its channels drive rock, ground or sand, and vegetation weights. Scale changes the sampled area around the image centre."),
		tr("Assign the allow/deny image in World > Terrain section maps > Tree mask. This controls where trees may be placed. Scale changes the sampled area around the image centre."),
		tr("Assign the road reference image in World > Terrain section maps > Road reference mask. It is shown as a decal on the terrain. Scale changes the sampled area around the image centre."),
		tr("Assign the building reference image in World > Terrain section maps > Building reference mask. It is shown as a decal on the terrain. Scale changes the sampled area around the image centre.")
	};
	FileSelectWidget* map_fields[5] = { heightmapURLFileSelectWidget, maskMapURLFileSelectWidget, treeMaskMapURLFileSelectWidget, roadMaskMapURLFileSelectWidget, buildingMaskMapURLFileSelectWidget };
	QCheckBox* map_enabled[5] = { heightmapEnabledCheckBox, maskMapEnabledCheckBox, treeMaskMapEnabledCheckBox, roadMaskMapEnabledCheckBox, buildingMaskMapEnabledCheckBox };
	for(int i=0; i<5; ++i)
	{
		map_clear_buttons[i]->setText(tr("Clear"));
		map_clear_buttons[i]->setToolTip(tr("Remove the %1 assigned under World > Terrain section maps for this section. The source file is not deleted.").arg(map_names[i]));
		map_scale_labels[i]->setText(tr("Scale"));
		map_scale_spin_boxes[i]->setToolTip(scale_guidance[i]);
		map_preview_labels[i]->setToolTip(tr("Preview of the assigned section map: %1").arg(map_names[i]));
		map_fields[i]->setToolTip(scale_guidance[i]);
		map_enabled[i]->setToolTip(scale_guidance[i]);
	}
}


void TerrainSpecSectionWidget::clearHeightMap()
{
	heightmapURLFileSelectWidget->setFilename(QString());
	heightmapEnabledCheckBox->setChecked(true);
}


void TerrainSpecSectionWidget::setMapPreviewPath(int index, const QString& path)
{
	if(index < 0 || index >= 5)
		return;
	QPixmap preview;
	if(!path.isEmpty())
		preview.load(path);
	if(!preview.isNull())
	{
		map_preview_labels[index]->setPixmap(preview.scaled(map_preview_labels[index]->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
		map_preview_labels[index]->setText(QString());
	}
	else
	{
		map_preview_labels[index]->setPixmap(QPixmap());
		map_preview_labels[index]->setText(index == 0 && path.endsWith(QStringLiteral(".exr"), Qt::CaseInsensitive) ? QStringLiteral("EXR") : tr("No preview"));
	}
}


QDoubleSpinBox* TerrainSpecSectionWidget::mapScaleSpinBox(int index) const
{
	return index >= 0 && index < 5 ? map_scale_spin_boxes[index] : NULL;
}
