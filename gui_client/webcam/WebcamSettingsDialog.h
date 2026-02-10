/*=====================================================================
WebcamSettingsDialog.h
----------------------
Copyright Glare Technologies Limited 2024 -
Dialog: resolution, FPS, quality (QCameraViewfinderSettings).
=====================================================================*/
#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSlider>
#include <QtWidgets/QLabel>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QGroupBox>
#include <QtCore/QList>
#include <QtCore/QSize>
#include <QtCore/QPair>
#include <QtMultimedia/QCameraViewfinderSettings>

class QCamera;

/*=====================================================================
WebcamSettingsDialog
--------------------
Preview: resolution, FPS. Photo: JPEG quality. Video: bitrate.
=====================================================================*/
class WebcamSettingsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit WebcamSettingsDialog(QCamera* camera, const QCameraViewfinderSettings& current,
		int photo_quality, qreal video_bitrate_mbps, QWidget* parent = nullptr);
	~WebcamSettingsDialog();

	QCameraViewfinderSettings getViewfinderSettings() const;
	int getPhotoQuality() const;
	qreal getVideoBitrateMbps() const;

signals:
	// Emitted when user changes resolution or FPS — apply instantly to preview
	void viewfinderSettingsChanged();

private:
	void populateFromCamera();

	QCamera* camera_;
	QComboBox* resolution_combo_;
	QComboBox* fps_combo_;
	QSlider* photo_quality_slider_;
	QLabel* photo_quality_label_;
	QDoubleSpinBox* video_bitrate_spin_;
	QList<QSize> resolutions_;
	QList<QPair<qreal, qreal>> fps_ranges_;
};
