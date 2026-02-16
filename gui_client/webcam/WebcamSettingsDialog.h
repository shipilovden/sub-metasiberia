/*=====================================================================
WebcamSettingsDialog.h
----------------------
Copyright Glare Technologies Limited 2024 -
Dialog: resolution, FPS, photo quality and video bitrate.
=====================================================================*/
#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSlider>
#include <QtWidgets/QLabel>
#include <QtWidgets/QDoubleSpinBox>
#include <QtCore/QList>
#include <QtCore/QSize>
#include <QtCore/QPair>

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtMultimedia/QCameraFormat>
using WebcamDialogViewfinderSettingsType = QCameraFormat;
#else
#include <QtMultimedia/QCameraViewfinderSettings>
using WebcamDialogViewfinderSettingsType = QCameraViewfinderSettings;
#endif

class QCamera;

class WebcamSettingsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit WebcamSettingsDialog(
		QCamera* camera,
		const WebcamDialogViewfinderSettingsType& current,
		int photo_quality,
		qreal video_bitrate_mbps,
		QWidget* parent = nullptr);
	~WebcamSettingsDialog();

	WebcamDialogViewfinderSettingsType getViewfinderSettings() const;
	int getPhotoQuality() const;
	qreal getVideoBitrateMbps() const;

signals:
	void viewfinderSettingsChanged();

private:
	void populateFromCamera();
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	void refreshFpsForCurrentResolution();
#endif

	QCamera* camera_;
	QComboBox* resolution_combo_;
	QComboBox* fps_combo_;
	QSlider* photo_quality_slider_;
	QLabel* photo_quality_label_;
	QDoubleSpinBox* video_bitrate_spin_;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QList<QCameraFormat> camera_formats_;
#else
	QList<QSize> resolutions_;
	QList<QPair<qreal, qreal>> fps_ranges_;
#endif
};

