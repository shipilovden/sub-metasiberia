/*=====================================================================
EnvironmentOptionsWidget.cpp
----------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "EnvironmentOptionsWidget.h"


#include "../qt/SignalBlocker.h"
#include <QtCore/QSettings>


EnvironmentOptionsWidget::EnvironmentOptionsWidget(QWidget* parent)
:	QWidget(parent),
	settings(NULL)
{
	setupUi(this);

	this->useLocalSunDirCheckBox->hide();

	this->sunThetaRealControl->setSliderSteps(200);

	connect(this->sunThetaRealControl,	SIGNAL(valueChanged(double)),	this, SLOT(settingChangedSlot()));
	connect(this->sunPhiRealControl,	SIGNAL(valueChanged(double)),	this, SLOT(settingChangedSlot()));
	connect(this->northernLightsCheckBox,	SIGNAL(toggled(bool)),		this, SLOT(settingChangedSlot()));
	
	printf("[EnvironmentOptionsWidget] Constructor: connected northernLightsCheckBox signal\n");
}


void EnvironmentOptionsWidget::init(QSettings* settings_) // settings should be set before this.
{
	settings = settings_;

	SignalBlocker::setValue(this->sunThetaRealControl, settings->value("environment_options/sun_theta", /*default val=*/40.0).toDouble());
	SignalBlocker::setValue(this->sunPhiRealControl,   settings->value("environment_options/sun_phi",   /*default val=*/0.0 ).toDouble());
	SignalBlocker::setChecked(this->northernLightsCheckBox, settings->value("environment_options/northern_lights", /*default val=*/true).toBool());
}


EnvironmentOptionsWidget::~EnvironmentOptionsWidget()
{
}


void EnvironmentOptionsWidget::settingChangedSlot()
{
	printf("[EnvironmentOptionsWidget] settingChangedSlot ENTERED\n");
	
	if(settings)
	{
		settings->setValue("environment_options/sun_theta", this->sunThetaRealControl->value());
		settings->setValue("environment_options/sun_phi",   this->sunPhiRealControl->value());
		settings->setValue("environment_options/northern_lights", this->northernLightsCheckBox->isChecked());
	}
	else
	{
		printf("[EnvironmentOptionsWidget] settingChangedSlot: settings is NULL!\n");
	}

	// Debug: print when setting changed
	const bool northern_lights_checked = this->northernLightsCheckBox->isChecked();
	printf("[EnvironmentOptionsWidget] settingChangedSlot: northern_lights = %s\n", 
		northern_lights_checked ? "true" : "false");

	emit settingChanged();
	printf("[EnvironmentOptionsWidget] settingChangedSlot: emitted settingChanged() signal\n");
}


bool EnvironmentOptionsWidget::getNorthernLightsEnabled() const
{
	return this->northernLightsCheckBox->isChecked();
}
