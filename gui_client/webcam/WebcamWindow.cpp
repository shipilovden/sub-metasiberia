/*=====================================================================
WebcamWindow.cpp
---------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "webcam/WebcamWindow.h"
#include "webcam/WebcamVideoView.h"
#include "webcam/WebcamControlPanel.h"
#include "webcam/WebcamSettingsDialog.h"
#include "../../utils/PlatformUtils.h"
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QLabel>
#include <QtCore/QDir>
#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtMultimedia/QCamera>
#include <QtMultimedia/QCameraInfo>
#include <QtMultimedia/QCameraImageCapture>
#include <QtMultimedia/QMediaRecorder>
#include <QtMultimedia/QCameraViewfinderSettings>
#include <QtMultimedia/QImageEncoderSettings>
#include <QtMultimedia/QVideoEncoderSettings>
#include <QtMultimedia/QMultimedia>
#include <QtMultimediaWidgets/QCameraViewfinder>
#include <QtCore/QUrl>
#include <QtCore/QFileInfo>
#include <QtGui/QImage>
#include <QtGui/QDesktopServices>

WebcamWindow::WebcamWindow(QWidget* parent)
	: QWidget(parent)
	, video_view_(nullptr)
	, preview_info_label_(nullptr)
	, panel_(nullptr)
	, recording_(false)
	, pending_start_(false)
	, photo_quality_(85)
	, video_bitrate_mbps_(5.0)
{
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	video_view_ = new WebcamVideoView(this);
	video_view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	layout->addWidget(video_view_, 1);

	preview_info_label_ = new QLabel(this);
	preview_info_label_->setObjectName("webcamPreviewInfoLabel");
	preview_info_label_->setStyleSheet("color: #555; font-size: 11px; padding: 2px 6px;");
	preview_info_label_->setText(tr("Preview: —"));
	layout->addWidget(preview_info_label_);

	panel_ = new WebcamControlPanel(this);
	layout->addWidget(panel_);

	refreshCameraList();
	panel_->setControlsEnabled(!camera_infos_.isEmpty());
	video_view_->setPlaceholderVisible(true);

	connect(panel_->enableCheckBox(), &QCheckBox::toggled, this, &WebcamWindow::onEnableToggled);
	connect(panel_->cameraComboBox(), QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WebcamWindow::onCameraIndexChanged);
	connect(panel_->photoButton(), &QPushButton::clicked, this, &WebcamWindow::onPhotoClicked);
	connect(panel_->recordButton(), &QPushButton::clicked, this, &WebcamWindow::onRecordClicked);
	connect(panel_->stopButton(), &QPushButton::clicked, this, &WebcamWindow::onStopClicked);
	connect(panel_->openFolderButton(), &QPushButton::clicked, this, &WebcamWindow::onOpenFolderClicked);
	connect(panel_->settingsButton(), &QPushButton::clicked, this, &WebcamWindow::onSettingsClicked);
}

WebcamWindow::~WebcamWindow()
{
	stopCamera();
}

bool WebcamWindow::isWebcamEnabled() const
{
	return panel_ && panel_->enableCheckBox()->isChecked();
}

void WebcamWindow::setWebcamEnabled(bool enabled)
{
	if (panel_)
		panel_->enableCheckBox()->setChecked(enabled);
}

void WebcamWindow::refreshCameraList()
{
	camera_infos_ = QCameraInfo::availableCameras();
	QComboBox* combo = panel_->cameraComboBox();
	combo->blockSignals(true);
	combo->clear();
	for (const QCameraInfo& info : camera_infos_)
		combo->addItem(info.description(), info.deviceName());
	combo->blockSignals(false);
	panel_->setControlsEnabled(!camera_infos_.isEmpty());
	if (camera_infos_.isEmpty())
		panel_->setStatusText(tr("• No cameras found"), false);
}

void WebcamWindow::startCamera()
{
	if (camera_infos_.isEmpty())
		return;
	int idx = panel_->cameraComboBox()->currentIndex();
	if (idx < 0 || idx >= camera_infos_.size())
		idx = 0;
	const QCameraInfo& info = camera_infos_.at(idx);
	camera_.reset(new QCamera(info));
	image_capture_.reset(new QCameraImageCapture(camera_.data()));
	// CaptureToBuffer + QImage::save() — бэкенд Windows часто не пишет файл сам (Could not save image to file)
	image_capture_->setCaptureDestination(QCameraImageCapture::CaptureToBuffer);
	media_recorder_.reset(new QMediaRecorder(camera_.data()));
	media_recorder_->setMuted(true);  // без аудио — WMF на Windows часто падает при захвате микрофона

	connect(camera_.data(), &QCamera::errorOccurred, this, &WebcamWindow::onCameraError);
	connect(camera_.data(), &QCamera::stateChanged, this, &WebcamWindow::onCameraStateChanged);
	connect(media_recorder_.data(), &QMediaRecorder::stateChanged, this, &WebcamWindow::onRecorderStateChanged);
	connect(media_recorder_.data(), QOverload<QMediaRecorder::Error>::of(&QMediaRecorder::error), this, &WebcamWindow::onRecorderError);
	connect(image_capture_.data(), &QCameraImageCapture::imageCaptured, this, &WebcamWindow::onImageCaptured);
	connect(image_capture_.data(), &QCameraImageCapture::imageSaved, this, &WebcamWindow::onImageSaved);
	connect(image_capture_.data(), QOverload<int, QCameraImageCapture::Error, const QString&>::of(&QCameraImageCapture::error), this, &WebcamWindow::onImageCaptureError);

	camera_->setViewfinder(video_view_->viewfinder());
	applyPhotoEncodingSettings();
	camera_->setCaptureMode(QCamera::CaptureViewfinder);
	video_view_->setCamera(camera_.data());
	video_view_->setPlaceholderVisible(false);
	// Load first; in LoadedState we apply viewfinder settings then start() — improves format support on Windows
	pending_start_ = true;
	camera_->load();
	updateStatusLabel();
}

void WebcamWindow::stopCamera()
{
	pending_start_ = false;
	pending_photo_path_.clear();
	next_photo_save_path_.clear();
	if (media_recorder_ && media_recorder_->state() != QMediaRecorder::StoppedState)
		media_recorder_->stop();
	recording_ = false;
	panel_->setRecordingState(false);
	if (camera_)
		camera_->stop();
	video_view_->setCamera(nullptr);
	video_view_->setPlaceholderVisible(true);
	camera_.reset();
	image_capture_.reset();
	media_recorder_.reset();
	panel_->setStatusText(tr("• Inactive"), false);
}

void WebcamWindow::applyViewfinderSettings(const QCameraViewfinderSettings& s)
{
	viewfinder_settings_ = s;
	// Apply to running camera so preview updates immediately
	if (camera_ && camera_->state() == QCamera::ActiveState && s.resolution().isValid())
		camera_->setViewfinderSettings(s);
}

static QMultimedia::EncodingQuality qualityFromPercent(int percent)
{
	if (percent <= 20) return QMultimedia::VeryLowQuality;
	if (percent <= 40) return QMultimedia::LowQuality;
	if (percent <= 60) return QMultimedia::NormalQuality;
	if (percent <= 80) return QMultimedia::HighQuality;
	return QMultimedia::VeryHighQuality;
}

void WebcamWindow::applyPhotoEncodingSettings()
{
	if (!image_capture_)
		return;
	QImageEncoderSettings s;
	s.setCodec("image/jpeg");
	s.setQuality(qualityFromPercent(photo_quality_));
	image_capture_->setEncodingSettings(s);
}

void WebcamWindow::applyVideoEncodingSettings()
{
	if (!media_recorder_)
		return;
	QVideoEncoderSettings s;
	QStringList codecs = media_recorder_->supportedVideoCodecs();
	if (!codecs.isEmpty())
		s.setCodec(codecs.first());
	s.setBitRate(static_cast<int>(video_bitrate_mbps_ * 1000000));
	media_recorder_->setVideoSettings(s);
}

void WebcamWindow::updateStatusLabel()
{
	if (recording_)
		panel_->setStatusText(tr("• Recording…"), true);
	else if (camera_ && camera_->state() == QCamera::ActiveState)
		panel_->setStatusText(tr("• Active"), false);
	updatePreviewInfoLabel();
}

void WebcamWindow::updatePreviewInfoLabel()
{
	if (!preview_info_label_)
		return;
	if (!camera_ || camera_->state() != QCamera::ActiveState)
	{
		preview_info_label_->setText(tr("Preview: —"));
		return;
	}
	QCameraViewfinderSettings s = camera_->viewfinderSettings();
	QSize res = s.resolution();
	qreal minF = s.minimumFrameRate();
	qreal maxF = s.maximumFrameRate();
	QString resStr = res.isValid() ? QString("%1 × %2").arg(res.width()).arg(res.height()) : tr("—");
	QString fpsStr = (minF > 0 || maxF > 0) ? QString("%1 FPS").arg(qRound(maxF > 0 ? maxF : minF)) : QString();
	QString info = resStr;
	if (!fpsStr.isEmpty())
		info += ", " + fpsStr;
	preview_info_label_->setText(tr("Preview: %1").arg(info));
}

void WebcamWindow::onEnableToggled(bool checked)
{
	if (checked)
		startCamera();
	else
		stopCamera();
}

void WebcamWindow::onCameraIndexChanged(int index)
{
	if (!panel_->enableCheckBox()->isChecked())
		return;
	// Restart with new camera
	stopCamera();
	startCamera();
}

QString WebcamWindow::getWebcamSaveDirectory() const
{
	// Standard folder: C:\Users\<user>\AppData\Roaming\Cyberspace\screenshots — photos and videos
	std::string dir = PlatformUtils::getOrCreateAppDataDirectory("Cyberspace");
	QString path = QString::fromStdString(dir) + "/screenshots";
	if (!QDir().exists(path))
		QDir().mkpath(path);
	return path;
}

void WebcamWindow::onPhotoClicked()
{
	if (!camera_ || !image_capture_)
		return;
	if (!pending_photo_path_.isEmpty())
		return;  // already waiting for capture
	QString path = getWebcamSaveDirectory();
	QString file = QDir(path).absoluteFilePath("metasiberia_webcam_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".jpg");
	applyPhotoEncodingSettings();
	camera_->setCaptureMode(QCamera::CaptureStillImage);
	if (image_capture_->isReadyForCapture()) {
		next_photo_save_path_ = file;
		camera_->searchAndLock();
		int id = image_capture_->capture(QString());  // buffer capture, save in onImageCaptured
		camera_->unlock();
		if (id < 0) {
			next_photo_save_path_.clear();
			camera_->setCaptureMode(QCamera::CaptureViewfinder);
			panel_->setStatusText(tr("• Photo failed to start"), true);
			QMessageBox::warning(this, tr("Webcam"), tr("Could not start photo capture. Check that the folder exists and is writable:\n%1").arg(path));
			return;
		}
		panel_->setStatusText(tr("• Saving…"), false);
	} else {
		pending_photo_path_ = file;
		connect(image_capture_.data(), &QCameraImageCapture::readyForCaptureChanged, this, &WebcamWindow::onReadyForCapture);
		panel_->setStatusText(tr("• Preparing…"), false);
	}
}

void WebcamWindow::onReadyForCapture(bool ready)
{
	if (!ready || pending_photo_path_.isEmpty() || !camera_ || !image_capture_)
		return;
	disconnect(image_capture_.data(), &QCameraImageCapture::readyForCaptureChanged, this, &WebcamWindow::onReadyForCapture);
	QString file = pending_photo_path_;
	pending_photo_path_.clear();
	next_photo_save_path_ = file;
	camera_->searchAndLock();
	int id = image_capture_->capture(QString());
	camera_->unlock();
	if (id < 0) {
		next_photo_save_path_.clear();
		camera_->setCaptureMode(QCamera::CaptureViewfinder);
		panel_->setStatusText(tr("• Photo failed to start"), true);
		QMessageBox::warning(this, tr("Webcam"), tr("Could not start photo capture."));
		return;
	}
	panel_->setStatusText(tr("• Saving…"), false);
}

void WebcamWindow::onImageCaptured(int id, const QImage& image)
{
	Q_UNUSED(id);
	if (next_photo_save_path_.isEmpty() || !camera_)
		return;
	QString path = next_photo_save_path_;
	next_photo_save_path_.clear();
	if (camera_)
		camera_->setCaptureMode(QCamera::CaptureViewfinder);
	if (image.isNull()) {
		panel_->setStatusText(tr("• Photo failed"), true);
		QMessageBox::warning(this, tr("Webcam"), tr("Photo could not be saved. No image data."));
		return;
	}
	int quality = qBound(1, photo_quality_, 100);
	if (!image.save(path, "JPEG", quality)) {
		panel_->setStatusText(tr("• Photo failed"), true);
		QMessageBox::warning(this, tr("Webcam"), tr("Could not save image to file:\n%1").arg(path));
		return;
	}
	panel_->setStatusText(tr("• Saved: %1").arg(QFileInfo(path).fileName()), false);
	panel_->statusLabel()->setToolTip(tr("Saved to: %1").arg(path));
	QTimer::singleShot(4000, this, [this]() { updateStatusLabel(); panel_->statusLabel()->setToolTip(QString()); });
}

void WebcamWindow::onImageSaved(int id, const QString& path)
{
	Q_UNUSED(id);
	if (camera_)
		camera_->setCaptureMode(QCamera::CaptureViewfinder);
	// При CaptureToBuffer сохранение делаем в onImageCaptured; imageSaved может не вызываться или path пустой
	if (!path.isEmpty())
	{
		panel_->setStatusText(tr("• Saved: %1").arg(QFileInfo(path).fileName()), false);
		panel_->statusLabel()->setToolTip(tr("Saved to: %1").arg(path));
		QTimer::singleShot(4000, this, [this]() { updateStatusLabel(); panel_->statusLabel()->setToolTip(QString()); });
	}
}

void WebcamWindow::onImageCaptureError(int id, QCameraImageCapture::Error error, const QString& errorString)
{
	Q_UNUSED(id);
	next_photo_save_path_.clear();
	if (camera_)
		camera_->setCaptureMode(QCamera::CaptureViewfinder);
	panel_->setStatusText(tr("• Photo failed"), true);
	QMessageBox::warning(this, tr("Webcam"), tr("Photo could not be saved: %1").arg(errorString.isEmpty() ? QString::number(static_cast<int>(error)) : errorString));
}

void WebcamWindow::onRecordClicked()
{
	if (!media_recorder_ || !camera_)
		return;
	QString path = getWebcamSaveDirectory();
	QString baseName = "metasiberia_webcam_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
	// На Windows WMF часто не стартует с MP4 — пробуем WMV (нативный контейнер)
	QStringList containers = media_recorder_->supportedContainers();
	QString ext = "mp4";
	QString mime;
	if (containers.contains("video/x-ms-wmv") || containers.contains("wmv")) {
		ext = "wmv";
		mime = "video/x-ms-wmv";
	} else if (containers.contains("video/mp4") || containers.contains("mp4")) {
		ext = "mp4";
		mime = "video/mp4";
	} else if (!containers.isEmpty()) {
		mime = containers.first();
		ext = (mime.contains("wmv", Qt::CaseInsensitive)) ? "wmv" : "mp4";
	}
#if defined(Q_OS_WIN)
	// WMF часто возвращает пустой supportedContainers — принудительно пробуем WMV
	if (mime.isEmpty()) {
		mime = "video/x-ms-wmv";
		ext = "wmv";
	}
#endif
	QString file = QDir(path).absoluteFilePath(baseName + "." + ext);
	last_video_path_ = file;
	last_recorder_error_.clear();
	if (!mime.isEmpty())
		media_recorder_->setContainerFormat(mime);
	applyVideoEncodingSettings();
	media_recorder_->setOutputLocation(QUrl::fromLocalFile(file));
	// Небольшая задержка перед record() — даём пайплайну камеры стабилизироваться (часто помогает на Windows)
	panel_->setRecordingState(true);
	panel_->setStatusText(tr("• Starting…"), false);
	QTimer::singleShot(350, this, [this]() {
		if (!media_recorder_ || last_video_path_.isEmpty()) return;
		media_recorder_->record();
	});
}

void WebcamWindow::onOpenFolderClicked()
{
	QDesktopServices::openUrl(QUrl::fromLocalFile(getWebcamSaveDirectory()));
}

void WebcamWindow::onStopClicked()
{
	if (media_recorder_)
		media_recorder_->stop();
	if (!last_video_path_.isEmpty() && !recording_)
	{
		panel_->setStatusText(tr("• Recording failed"), true);
		QString msg = tr("Video recording did not start. Check settings and try again.");
		if (!last_recorder_error_.isEmpty())
			msg += "\n\n" + last_recorder_error_;
		QMessageBox::warning(this, tr("Webcam"), msg);
		last_video_path_.clear();
		last_recorder_error_.clear();
	}
	recording_ = false;
	panel_->setRecordingState(false);
	updateStatusLabel();
}

void WebcamWindow::onSettingsClicked()
{
	if (camera_infos_.isEmpty())
	{
		QMessageBox::information(this, tr("Webcam Settings"), tr("No camera available."));
		return;
	}
	// Ensure we have a camera (loaded) to query supported settings
	if (!camera_)
	{
		int idx = qMax(0, panel_->cameraComboBox()->currentIndex());
		QCamera temp(camera_infos_.at(idx));
		temp.load();
		WebcamSettingsDialog dlg(&temp, viewfinder_settings_, photo_quality_, video_bitrate_mbps_, this);
		if (dlg.exec() == QDialog::Accepted)
		{
			applyViewfinderSettings(dlg.getViewfinderSettings());
			photo_quality_ = dlg.getPhotoQuality();
			video_bitrate_mbps_ = dlg.getVideoBitrateMbps();
			// Settings stored; will apply when user enables webcam (onCameraStateChanged)
		}
		return;
	}
	WebcamSettingsDialog dlg(camera_.data(), viewfinder_settings_, photo_quality_, video_bitrate_mbps_, this);
	connect(&dlg, &WebcamSettingsDialog::viewfinderSettingsChanged, this, [this, &dlg]() {
		applyViewfinderSettings(dlg.getViewfinderSettings());
	});
	if (dlg.exec() == QDialog::Accepted)
	{
		applyViewfinderSettings(dlg.getViewfinderSettings());
		photo_quality_ = dlg.getPhotoQuality();
		video_bitrate_mbps_ = dlg.getVideoBitrateMbps();
		applyPhotoEncodingSettings();
		applyVideoEncodingSettings();
		// Restart camera so new viewfinder settings apply to preview (and next photo/recording)
		stopCamera();
		startCamera();
	}
}

void WebcamWindow::onRecorderStateChanged()
{
	if (!media_recorder_)
		return;
	QMediaRecorder::State state = media_recorder_->state();
	if (state == QMediaRecorder::RecordingState)
	{
		recording_ = true;
		panel_->setRecordingState(true);
		updateStatusLabel();
	}
	else if (state == QMediaRecorder::StoppedState)
	{
		recording_ = false;
		panel_->setRecordingState(false);
		if (!last_video_path_.isEmpty())
		{
			if (QFileInfo(last_video_path_).exists())
			{
				panel_->setStatusText(tr("• Saved: %1").arg(QFileInfo(last_video_path_).fileName()), false);
				panel_->statusLabel()->setToolTip(tr("Saved to: %1").arg(last_video_path_));
				QTimer::singleShot(4000, this, [this]() { updateStatusLabel(); panel_->statusLabel()->setToolTip(QString()); });
			}
			else
			{
				panel_->setStatusText(tr("• Recording failed"), true);
				QMessageBox::warning(this, tr("Webcam"), tr("Video was not saved to file.\n%1").arg(last_video_path_));
			}
			last_video_path_.clear();
		}
		updateStatusLabel();
	}
}

void WebcamWindow::onRecorderError(QMediaRecorder::Error error)
{
	Q_UNUSED(error);
	recording_ = false;
	panel_->setRecordingState(false);
	last_recorder_error_ = media_recorder_ ? media_recorder_->errorString() : QString();
	last_video_path_.clear();
	QString err = last_recorder_error_;
	panel_->setStatusText(tr("• Recording failed"), true);
	QMessageBox::warning(this, tr("Webcam"), tr("Video recording error: %1").arg(err.isEmpty() ? tr("Unknown error") : err));
	updateStatusLabel();
}

void WebcamWindow::onCameraStateChanged(QCamera::State state)
{
	if (pending_start_ && state == QCamera::LoadedState)
	{
		pending_start_ = false;
		// Apply viewfinder settings while loaded, before start — required for format change on many backends
		if (viewfinder_settings_.resolution().isValid())
			camera_->setViewfinderSettings(viewfinder_settings_);
		camera_->start();
	}
	updateStatusLabel();
}

void WebcamWindow::onCameraError(QCamera::Error error)
{
	Q_UNUSED(error);
	if (!camera_)
		return;
	QString err = camera_->errorString();
	if (err.contains("preview format", Qt::CaseInsensitive) || err.contains("configure preview", Qt::CaseInsensitive))
	{
		// Clear so next start uses camera default; avoid popup
		viewfinder_settings_ = QCameraViewfinderSettings();
		return;
	}
	QMessageBox::warning(this, tr("Webcam"), tr("Camera error: %1").arg(err));
	viewfinder_settings_ = QCameraViewfinderSettings();
}
