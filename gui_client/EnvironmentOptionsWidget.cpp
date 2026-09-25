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
	this->cloudBottomZRealControl->setSliderSteps(200);
	this->cloudTopZRealControl->setSliderSteps(200);
	this->cloudCoverageRealControl->setSliderSteps(100);
	this->cloudDensityRealControl->setSliderSteps(100);
	this->cloudWindSpeedRealControl->setSliderSteps(200);

	connect(this->sunThetaRealControl,	SIGNAL(valueChanged(double)),	this, SLOT(settingChangedSlot()));
	connect(this->sunPhiRealControl,	SIGNAL(valueChanged(double)),	this, SLOT(settingChangedSlot()));
	connect(this->northernLightsCheckBox,	SIGNAL(toggled(bool)),		this, SLOT(settingChangedSlot()));
	connect(this->volumetricCloudsCheckBox, SIGNAL(toggled(bool)),		this, SLOT(settingChangedSlot()));
	connect(this->cloudBottomZRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingChangedSlot()));
	connect(this->cloudTopZRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingChangedSlot()));
	connect(this->cloudCoverageRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingChangedSlot()));
	connect(this->cloudDensityRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingChangedSlot()));
	connect(this->cloudWindSpeedRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingChangedSlot()));
	
	printf("[EnvironmentOptionsWidget] Constructor: connected northernLightsCheckBox signal\n");
}


void EnvironmentOptionsWidget::init(QSettings* settings_) // settings should be set before this.
{
	settings = settings_;

	SignalBlocker::setValue(this->sunThetaRealControl, settings->value("environment_options/sun_theta", /*default val=*/40.0).toDouble());
	SignalBlocker::setValue(this->sunPhiRealControl,   settings->value("environment_options/sun_phi",   /*default val=*/0.0 ).toDouble());
	SignalBlocker::setChecked(this->northernLightsCheckBox, settings->value("environment_options/northern_lights", /*default val=*/true).toBool());
	SignalBlocker::setChecked(this->volumetricCloudsCheckBox, settings->value("environment_options/volumetric_clouds", /*default val=*/true).toBool());
	SignalBlocker::setValue(this->cloudBottomZRealControl, settings->value("environment_options/cloud_bottom_z", /*default val=*/1000.0).toDouble());
	SignalBlocker::setValue(this->cloudTopZRealControl, settings->value("environment_options/cloud_top_z", /*default val=*/2200.0).toDouble());
	SignalBlocker::setValue(this->cloudCoverageRealControl, settings->value("environment_options/cloud_coverage", /*default val=*/0.48).toDouble());
	SignalBlocker::setValue(this->cloudDensityRealControl, settings->value("environment_options/cloud_density", /*default val=*/0.0012).toDouble());
	SignalBlocker::setValue(this->cloudWindSpeedRealControl, settings->value("environment_options/cloud_wind_speed", /*default val=*/20.0).toDouble());
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
		settings->setValue("environment_options/volumetric_clouds", this->volumetricCloudsCheckBox->isChecked());
		settings->setValue("environment_options/cloud_bottom_z", this->cloudBottomZRealControl->value());
		settings->setValue("environment_options/cloud_top_z", this->cloudTopZRealControl->value());
		settings->setValue("environment_options/cloud_coverage", this->cloudCoverageRealControl->value());
		settings->setValue("environment_options/cloud_density", this->cloudDensityRealControl->value());
		settings->setValue("environment_options/cloud_wind_speed", this->cloudWindSpeedRealControl->value());
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


bool EnvironmentOptionsWidget::getVolumetricCloudsEnabled() const
{
	return this->volumetricCloudsCheckBox->isChecked();
}


double EnvironmentOptionsWidget::getCloudBottomZ() const
{
	return this->cloudBottomZRealControl->value();
}


double EnvironmentOptionsWidget::getCloudTopZ() const
{
	return this->cloudTopZRealControl->value();
}


double EnvironmentOptionsWidget::getCloudCoverage() const
{
	return this->cloudCoverageRealControl->value();
}


double EnvironmentOptionsWidget::getCloudDensity() const
{
	return this->cloudDensityRealControl->value();
}


double EnvironmentOptionsWidget::getCloudWindSpeed() const
{
	return this->cloudWindSpeedRealControl->value();
}
