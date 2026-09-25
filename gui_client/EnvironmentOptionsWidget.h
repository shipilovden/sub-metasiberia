/*=====================================================================
EnvironmentOptionsWidget.h
--------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "ui_EnvironmentOptionsWidget.h"


class QSettings;


class EnvironmentOptionsWidget : public QWidget, public Ui::EnvironmentOptionsWidget
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	EnvironmentOptionsWidget(QWidget* parent);
	~EnvironmentOptionsWidget();

	void init(QSettings* settings_);
	
	bool getNorthernLightsEnabled() const;
	bool getVolumetricCloudsEnabled() const;
	double getCloudBottomZ() const;
	double getCloudTopZ() const;
	double getCloudCoverage() const;
	double getCloudDensity() const;
	double getCloudWindSpeed() const;

signals:;
	void settingChanged();

protected slots:
	void settingChangedSlot();

private:
	QSettings* settings;
};
