/*=====================================================================
WebcamWindow.h
--------------
Copyright Glare Technologies Limited 2024 -
Main webcam widget: video view + control panel, QCamera integration.
=====================================================================*/
#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QScopedPointer>

#include <QtMultimedia/QCamera>
#include <QtMultimedia/QCameraInfo>
#include <QtMultimedia/QCameraViewfinderSettings>
#include <QtMultimedia/QCameraImageCapture>
#include <QtMultimedia/QMediaRecorder>
#include <QtCore/QList>
#include <QtGui/QImage>

class WebcamVideoView;
class WebcamControlPanel;
class WebcamSettingsDialog;
class QLabel;

/*=====================================================================
WebcamWindow
------------
Central area: WebcamVideoView. Bottom: WebcamControlPanel.
Owns QCamera, QCameraImageCapture, QMediaRecorder; wires enable, camera
selection, photo, record, stop, settings.
=====================================================================*/
class WebcamWindow : public QWidget
{
	Q_OBJECT
public:
	explicit WebcamWindow(QWidget* parent = nullptr);
	~WebcamWindow();

	// Whether webcam is enabled (checkbox state).
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
	void onCameraStateChanged(QCamera::State state);
	void onCameraError(QCamera::Error error);
	void onImageSaved(int id, const QString& path);
	void onImageCaptured(int id, const QImage& image);
	void onImageCaptureError(int id, QCameraImageCapture::Error error, const QString& errorString);
	void onReadyForCapture(bool ready);

private:
	QString getWebcamSaveDirectory() const;
	void refreshCameraList();
	void startCamera();
	void stopCamera();
	void applyViewfinderSettings(const QCameraViewfinderSettings& s);
	void applyPhotoEncodingSettings();
	void applyVideoEncodingSettings();
	void updateStatusLabel();
	void updatePreviewInfoLabel();

	WebcamVideoView* video_view_;
	QLabel* preview_info_label_;
	WebcamControlPanel* panel_;
	QScopedPointer<QCamera> camera_;
	QScopedPointer<QCameraImageCapture> image_capture_;
	QScopedPointer<QMediaRecorder> media_recorder_;
	QList<QCameraInfo> camera_infos_;
	QCameraViewfinderSettings viewfinder_settings_;
	bool recording_;
	bool pending_start_;  // load() was called; on LoadedState we apply viewfinder settings then start()
	QString last_video_path_;  // path of last recorded video (to show in status when stopped)
	QString pending_photo_path_;  // when set, we wait for readyForCapture then capture to this path
	QString next_photo_save_path_;  // when capturing to buffer, path where to save the QImage
	int photo_quality_;        // 1-100 for JPEG
	qreal video_bitrate_mbps_; // video encoding bitrate in Mbps
};
