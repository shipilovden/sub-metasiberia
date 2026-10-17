/*=====================================================================
TerrainSpecSectionWidget.cpp
----------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "TerrainSpecSectionWidget.h"

#include "LucideIconUtils.h"
#include <qt/QtUtils.h>


#include "../qt/SignalBlocker.h"
#include <QtCore/QSettings>
#include <QtCore/QCoreApplication>
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
		map_preview_labels[i] = new QLabel(this);
		map_preview_labels[i]->setObjectName(QString("sectionMapPreview%1").arg(i));
		// Match the compact preview used by the surface material rows.  A square
		// preview made each map row much taller and forced the section form to wrap.
		map_preview_labels[i]->setFixedSize(56, 38);
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
		map_scale_spin_boxes[i]->setMinimumWidth(72);
		map_scale_spin_boxes[i]->setMaximumWidth(88);
		map_scale_spin_boxes[i]->setToolTip(tr("Controls how much of this map is used in the section. Higher values zoom in; lower values zoom out."));
		map_scale_labels[i] = new QLabel(tr("Scale"), this);
		map_scale_labels[i]->setFixedWidth(10);
		map_scale_labels[i]->setBuddy(map_scale_spin_boxes[i]);
		map_rows[i]->insertWidget(1, map_preview_labels[i]);
		map_rows[i]->insertWidget(2, map_scale_labels[i]);
		map_rows[i]->insertWidget(3, map_scale_spin_boxes[i]);
		map_scale_y_labels[i] = new QLabel(tr("Y"), this);
		map_scale_y_labels[i]->setObjectName(QString("sectionMapScaleYLabel%1").arg(i));
		map_scale_y_labels[i]->setFixedWidth(10);
		map_scale_y_spin_boxes[i] = new QDoubleSpinBox(this);
		map_scale_y_spin_boxes[i]->setObjectName(QString("sectionMapScaleY%1").arg(i));
		map_scale_y_spin_boxes[i]->setRange(0.01, 100.0);
		map_scale_y_spin_boxes[i]->setDecimals(2);
		map_scale_y_spin_boxes[i]->setSingleStep(0.1);
		map_scale_y_spin_boxes[i]->setValue(1.0);
		map_scale_y_spin_boxes[i]->setSuffix(QStringLiteral(" ×"));
		map_scale_y_spin_boxes[i]->setMinimumWidth(72);
		map_scale_y_spin_boxes[i]->setMaximumWidth(88);
		map_scale_y_spin_boxes[i]->setToolTip(tr("Controls map tiling on the Y axis. Higher values zoom in; lower values zoom out."));
		map_scale_y_labels[i]->setBuddy(map_scale_y_spin_boxes[i]);
		map_rows[i]->insertWidget(4, map_scale_y_labels[i]);
		map_rows[i]->insertWidget(5, map_scale_y_spin_boxes[i]);
		map_scale_labels[i]->setText(tr("X"));
		map_scale_spin_boxes[i]->setToolTip(tr("Controls map tiling on the X axis. Higher values zoom in; lower values zoom out."));
		map_fields[i]->setMinimumWidth(0);
		map_fields[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		map_clear_buttons[i] = new QPushButton(this);
		map_rows[i]->addWidget(map_clear_buttons[i]);
		map_rows[i]->setSpacing(4);
		map_rows[i]->setStretch(6, 1); // Keep the file path as the only flexible column.
		if(QPushButton* browse_button = map_fields[i]->findChild<QPushButton*>(QStringLiteral("fileSelectButton")))
		{
			browse_button->setText(QString());
			browse_button->setFixedWidth(30);
			browse_button->setToolTip(tr("Browse"));
			LucideIconUtils::setButtonIcon(browse_button, LucideIconUtils::directoryForBasePath(QtUtils::toStdString(QCoreApplication::applicationDirPath())), QStringLiteral("folder-open"), palette().color(QPalette::ButtonText), 16);
		}
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
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	formLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
	for(QLabel* child_label : findChildren<QLabel*>())
		child_label->setWordWrap(true);
	for(int i=0; i<5; ++i)
	{
		// Keep the same fixed preview and X/Y columns in every row. Only the
		// filename field should shrink when the settings dock is narrow.
		map_rows[i]->parentWidget()->setMinimumWidth(0);
		map_fields[i]->setMinimumWidth(0);
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
	for(int i=0; i<5; ++i) map_scale_y_spin_boxes[i]->setEnabled(editable);
}

void TerrainSpecSectionWidget::retranslateMapClearButtons()
{
	const QString map_names[5] = {
		tr("Height map"), tr("Surface material mask"), tr("Tree mask"), tr("Road reference mask"), tr("Building reference mask")
	};
	QLabel* map_row_labels[5] = { label, label_2, label_5, label_6, label_7 };
	const QString numbered_map_names[5] = {
		tr("1. Height map"), tr("2. Mask map"), tr("3. Tree mask"), tr("4. Road mask"), tr("5. Building mask")
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
		map_row_labels[i]->setText(numbered_map_names[i]);
		map_clear_buttons[i]->setText(QString());
		map_clear_buttons[i]->setFixedWidth(30);
		LucideIconUtils::setButtonIcon(map_clear_buttons[i], LucideIconUtils::directoryForBasePath(QtUtils::toStdString(QCoreApplication::applicationDirPath())), QStringLiteral("trash-2"), palette().color(QPalette::ButtonText), 16);
		map_clear_buttons[i]->setToolTip(tr("Remove the %1 assigned under World > Terrain section maps for this section. The source file is not deleted.").arg(map_names[i]));
		map_scale_labels[i]->setText(tr("X"));
		map_scale_y_labels[i]->setText(tr("Y"));
		const QString axis_guidance = tr("Controls map tiling on the %1 axis. Higher values zoom in; lower values zoom out.");
		map_scale_spin_boxes[i]->setToolTip(axis_guidance.arg(tr("X")) + QStringLiteral(" ") + scale_guidance[i]);
		map_scale_y_spin_boxes[i]->setToolTip(axis_guidance.arg(tr("Y")) + QStringLiteral(" ") + scale_guidance[i]);
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
		// Match the image area used by the surface-material previews inside the
		// same 56 x 38 framed cell.
		map_preview_labels[index]->setPixmap(preview.scaled(52, 34, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		map_preview_labels[index]->setText(QString());
	}
	else
	{
		map_preview_labels[index]->setPixmap(QPixmap());
		map_preview_labels[index]->setText(QString());
	}
}


QDoubleSpinBox* TerrainSpecSectionWidget::mapScaleSpinBox(int index, bool y_axis) const
{
	return index >= 0 && index < 5 ? (y_axis ? map_scale_y_spin_boxes[index] : map_scale_spin_boxes[index]) : NULL;
}
