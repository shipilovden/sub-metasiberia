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
#include <QtWidgets/QSizePolicy>


TerrainSpecSectionWidget::TerrainSpecSectionWidget(QWidget* parent)
:	QWidget(parent)
{
	setupUi(this);

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
}
