/*=====================================================================
WebcamWindow.h
--------------
Copyright Glare Technologies Limited 2024 -
Main webcam widget: video view + control panel.
=====================================================================*/
#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QScopedPointer>
#include <QtCore/QList>
#include <QtGui/QImage>

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtMultimedia/QCamera>
#include <QtMultimedia/QCameraDevice>
#include <QtMultimedia/QCameraFormat>
#include <QtMultimedia/QImageCapture>
#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QMediaCaptureSession>
#include <QtMultimedia/QMediaRecorder>
using WebcamViewfinderSettingsType = QCameraFormat;
#else
#include <QtMultimedia/QCamera>
#include <QtMultimedia/QCameraInfo>
#include <QtMultimedia/QCameraViewfinderSettings>
#include <QtMultimedia/QCameraImageCapture>
#include <QtMultimedia/QMediaRecorder>
using WebcamViewfinderSettingsType = QCameraViewfinderSettings;
#endif

class WebcamVideoView;
class WebcamControlPanel;
class QLabel;

class WebcamWindow : public QWidget
{
	Q_OBJECT
public:
	explicit WebcamWindow(QWidget* parent = nullptr);
	~WebcamWindow();

	bool isWebcamEnabled() const;
	void setWebcamEnabled(bool enabled);

private slots:
	void onEnableToggled(bool checked);
	void onCameraIndexChanged(int index);
	void onPhotoClicked();
	void onRecordClicked();
	void onStopClicked();
	void onOpenFolderClicked();
	void onSettingsClicked();
	void onRecorderStateChanged();
	void onRecorderError();
	void onCameraStateChanged();
	void onCameraError();
	void onImageSaved(int id, const QString& path);
	void onImageCaptured(int id, const QImage& image);
	void onImageCaptureError(int id, int error, const QString& errorString);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
	void onReadyForCapture(bool ready);
#endif

private:
	QString getWebcamSaveDirectory() const;
	void refreshCameraList();
	void startCamera();
	void stopCamera();
	void applyViewfinderSettings(const WebcamViewfinderSettingsType& settings);
	void applyPhotoEncodingSettings();
	void applyVideoEncodingSettings();
	void updateStatusLabel();
	void updatePreviewInfoLabel();

	WebcamVideoView* video_view_;
	QLabel* preview_info_label_;
	WebcamControlPanel* panel_;
	QScopedPointer<QCamera> camera_;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QMediaDevices* media_devices_;
	QMediaCaptureSession capture_session_;
	QScopedPointer<QImageCapture> image_capture_;
	QList<QCameraDevice> camera_infos_;
#else
	QScopedPointer<QCameraImageCapture> image_capture_;
	QList<QCameraInfo> camera_infos_;
#endif
	QScopedPointer<QMediaRecorder> media_recorder_;
	WebcamViewfinderSettingsType viewfinder_settings_;
	bool recording_;
	bool pending_start_;
	QString last_video_path_;
	QString last_recorder_error_;
	QString pending_photo_path_;
	QString next_photo_save_path_;
	int photo_quality_;
	qreal video_bitrate_mbps_;
};
