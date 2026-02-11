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
#include <QtCore/QUrl>
#include <QtCore/QFileInfo>
#include <QtGui/QDesktopServices>

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtMultimedia/QMediaDevices>
#include <QtMultimediaWidgets/QVideoWidget>
#else
#include <QtMultimedia/QCameraInfo>
#include <QtMultimedia/QCameraViewfinderSettings>
#include <QtMultimedia/QImageEncoderSettings>
#include <QtMultimedia/QVideoEncoderSettings>
#include <QtMultimedia/QMultimedia>
#endif

WebcamWindow::WebcamWindow(QWidget* parent)
	: QWidget(parent)
	, video_view_(nullptr)
	, preview_info_label_(nullptr)
	, panel_(nullptr)
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	, media_devices_(nullptr)
#endif
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
	preview_info_label_->setText(tr("Preview: -"));
	layout->addWidget(preview_info_label_);

	panel_ = new WebcamControlPanel(this);
	layout->addWidget(panel_);

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	media_devices_ = new QMediaDevices(this);
	connect(media_devices_, &QMediaDevices::videoInputsChanged, this, &WebcamWindow::refreshCameraList);
#endif

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
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	camera_infos_.clear();
	const QList<QCameraDevice> devices = media_devices_ ? media_devices_->videoInputs() : QMediaDevices::videoInputs();
	for (const QCameraDevice& info : devices)
	{
		if (!info.isNull())
			camera_infos_.append(info);
	}
#else
	camera_infos_ = QCameraInfo::availableCameras();
#endif

	QComboBox* combo = panel_->cameraComboBox();
	combo->blockSignals(true);
	combo->clear();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	for (const QCameraDevice& info : camera_infos_)
	{
		const QString label = info.description().isEmpty() ? QString::fromUtf8(info.id()) : info.description();
		combo->addItem(label, info.id());
	}
	if (!camera_infos_.isEmpty())
	{
		const QCameraDevice def = QMediaDevices::defaultVideoInput();
		if (!def.isNull())
		{
			for (int i = 0; i < camera_infos_.size(); ++i)
			{
				if (camera_infos_[i].id() == def.id())
				{
					combo->setCurrentIndex(i);
					break;
				}
			}
		}
	}
#else
	for (const QCameraInfo& info : camera_infos_)
		combo->addItem(info.description(), info.deviceName());
#endif

	combo->blockSignals(false);
	panel_->setControlsEnabled(!camera_infos_.isEmpty());
	if (camera_infos_.isEmpty())
		panel_->setStatusText(tr("No cameras found"), false);
}

void WebcamWindow::startCamera()
{
	if (camera_infos_.isEmpty())
		return;

	int index = panel_->cameraComboBox()->currentIndex();
	if (index < 0 || index >= camera_infos_.size())
		index = 0;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	const QCameraDevice& info = camera_infos_.at(index);
	camera_.reset(new QCamera(info));
	image_capture_.reset(new QImageCapture());
	media_recorder_.reset(new QMediaRecorder());

	capture_session_.setCamera(camera_.data());
	capture_session_.setImageCapture(image_capture_.data());
	capture_session_.setRecorder(media_recorder_.data());
	capture_session_.setVideoOutput(video_view_->videoWidget());

	connect(camera_.data(), &QCamera::activeChanged, this, [this](bool) { onCameraStateChanged(); });
	connect(camera_.data(), &QCamera::errorOccurred, this, [this](QCamera::Error, const QString&) { onCameraError(); });
	connect(media_recorder_.data(), &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState) { onRecorderStateChanged(); });
	connect(media_recorder_.data(), &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString&) { onRecorderError(); });
	connect(image_capture_.data(), &QImageCapture::imageCaptured, this, &WebcamWindow::onImageCaptured);
	connect(image_capture_.data(), &QImageCapture::imageSaved, this, &WebcamWindow::onImageSaved);
	connect(image_capture_.data(), &QImageCapture::errorOccurred, this, [this](int id, QImageCapture::Error error, const QString& errorString) { onImageCaptureError(id, static_cast<int>(error), errorString); });

	applyPhotoEncodingSettings();
	if (!viewfinder_settings_.isNull())
		camera_->setCameraFormat(viewfinder_settings_);
	video_view_->setCamera(camera_.data());
	video_view_->setPlaceholderVisible(false);
	camera_->start();

	// If camera did not become active shortly after start(), surface a clear status.
	QTimer::singleShot(1500, this, [this]() {
		if (camera_ && !camera_->isActive())
		{
			const QString err = camera_->errorString();
			panel_->setStatusText(err.isEmpty() ? tr("Camera failed to start") : err, true);
		}
	});
#else
	const QCameraInfo& info = camera_infos_.at(index);
	camera_.reset(new QCamera(info));
	image_capture_.reset(new QCameraImageCapture(camera_.data()));
	image_capture_->setCaptureDestination(QCameraImageCapture::CaptureToBuffer);
	media_recorder_.reset(new QMediaRecorder(camera_.data()));
	media_recorder_->setMuted(true);

	connect(camera_.data(), &QCamera::errorOccurred, this, &WebcamWindow::onCameraError);
	connect(camera_.data(), &QCamera::stateChanged, this, &WebcamWindow::onCameraStateChanged);
	connect(media_recorder_.data(), &QMediaRecorder::stateChanged, this, &WebcamWindow::onRecorderStateChanged);
	connect(media_recorder_.data(), QOverload<QMediaRecorder::Error>::of(&QMediaRecorder::error), this, [this](QMediaRecorder::Error) { onRecorderError(); });
	connect(image_capture_.data(), &QCameraImageCapture::imageCaptured, this, &WebcamWindow::onImageCaptured);
	connect(image_capture_.data(), &QCameraImageCapture::imageSaved, this, &WebcamWindow::onImageSaved);
	connect(image_capture_.data(), QOverload<int, QCameraImageCapture::Error, const QString&>::of(&QCameraImageCapture::error), this, [this](int id, QCameraImageCapture::Error error, const QString& errorString) { onImageCaptureError(id, static_cast<int>(error), errorString); });

	camera_->setViewfinder(video_view_->viewfinder());
	applyPhotoEncodingSettings();
	camera_->setCaptureMode(QCamera::CaptureViewfinder);
	video_view_->setCamera(camera_.data());
	video_view_->setPlaceholderVisible(false);
	pending_start_ = true;
	camera_->load();
#endif

	updateStatusLabel();
}

void WebcamWindow::stopCamera()
{
	pending_start_ = false;
	pending_photo_path_.clear();
	next_photo_save_path_.clear();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	if (media_recorder_ && media_recorder_->recorderState() != QMediaRecorder::StoppedState)
		media_recorder_->stop();
#else
	if (media_recorder_ && media_recorder_->state() != QMediaRecorder::StoppedState)
		media_recorder_->stop();
#endif

	recording_ = false;
	panel_->setRecordingState(false);
	if (camera_)
		camera_->stop();

	video_view_->setCamera(nullptr);
	video_view_->setPlaceholderVisible(true);

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	capture_session_.setVideoOutput(nullptr);
	capture_session_.setImageCapture(nullptr);
	capture_session_.setRecorder(nullptr);
	capture_session_.setCamera(nullptr);
#endif

	camera_.reset();
	image_capture_.reset();
	media_recorder_.reset();
	panel_->setStatusText(tr("Inactive"), false);
}

void WebcamWindow::applyViewfinderSettings(const WebcamViewfinderSettingsType& settings)
{
	viewfinder_settings_ = settings;
	if (!camera_)
		return;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	if (settings.isNull())
		return;

	// On Qt6/Windows, applying format while active is unreliable on some drivers.
	// Restarting camera guarantees immediate preview update.
	const bool was_active = camera_->isActive();
	if (was_active)
		camera_->stop();

	camera_->setCameraFormat(settings);

	if (was_active)
		camera_->start();
#else
	if (camera_->state() == QCamera::ActiveState && settings.resolution().isValid())
		camera_->setViewfinderSettings(settings);
#endif
}

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
static QImageCapture::Quality photoQualityFromPercent(int percent)
{
	if (percent <= 20) return QImageCapture::VeryLowQuality;
	if (percent <= 40) return QImageCapture::LowQuality;
	if (percent <= 60) return QImageCapture::NormalQuality;
	if (percent <= 80) return QImageCapture::HighQuality;
	return QImageCapture::VeryHighQuality;
}
#else
static QMultimedia::EncodingQuality photoQualityFromPercent(int percent)
{
	if (percent <= 20) return QMultimedia::VeryLowQuality;
	if (percent <= 40) return QMultimedia::LowQuality;
	if (percent <= 60) return QMultimedia::NormalQuality;
	if (percent <= 80) return QMultimedia::HighQuality;
	return QMultimedia::VeryHighQuality;
}
#endif

void WebcamWindow::applyPhotoEncodingSettings()
{
	if (!image_capture_)
		return;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	image_capture_->setQuality(photoQualityFromPercent(photo_quality_));
	image_capture_->setFileFormat(QImageCapture::JPEG);
#else
	QImageEncoderSettings settings;
	settings.setCodec("image/jpeg");
	settings.setQuality(photoQualityFromPercent(photo_quality_));
	image_capture_->setEncodingSettings(settings);
#endif
}

void WebcamWindow::applyVideoEncodingSettings()
{
	if (!media_recorder_)
		return;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	media_recorder_->setVideoBitRate(static_cast<int>(video_bitrate_mbps_ * 1000000));
#else
	QVideoEncoderSettings settings;
	QStringList codecs = media_recorder_->supportedVideoCodecs();
	if (!codecs.isEmpty())
		settings.setCodec(codecs.first());
	settings.setBitRate(static_cast<int>(video_bitrate_mbps_ * 1000000));
	media_recorder_->setVideoSettings(settings);
#endif
}

void WebcamWindow::updateStatusLabel()
{
	if (recording_)
		panel_->setStatusText(tr("Recording..."), true);
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	else if (camera_ && camera_->isActive())
#else
	else if (camera_ && camera_->state() == QCamera::ActiveState)
#endif
		panel_->setStatusText(tr("Active"), false);
	else
		panel_->setStatusText(tr("Inactive"), false);

	updatePreviewInfoLabel();
}

void WebcamWindow::updatePreviewInfoLabel()
{
	if (!preview_info_label_)
		return;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	if (!camera_ || !camera_->isActive())
	{
		preview_info_label_->setText(tr("Preview: -"));
		return;
	}

	QCameraFormat format = camera_->cameraFormat();
	QSize resolution = format.resolution();
	const qreal fps = format.maxFrameRate();
#else
	if (!camera_ || camera_->state() != QCamera::ActiveState)
	{
		preview_info_label_->setText(tr("Preview: -"));
		return;
	}

	QCameraViewfinderSettings format = camera_->viewfinderSettings();
	QSize resolution = format.resolution();
	const qreal fps = (format.maximumFrameRate() > 0) ? format.maximumFrameRate() : format.minimumFrameRate();
#endif

	QString info = resolution.isValid() ? QString("%1 x %2").arg(resolution.width()).arg(resolution.height()) : tr("-");
	if (fps > 0)
		info += QString(", %1 FPS").arg(qRound(fps));
	preview_info_label_->setText(tr("Preview: %1").arg(info));
}

void WebcamWindow::onEnableToggled(bool checked)
{
	if (checked)
	{
		if (camera_infos_.isEmpty())
			refreshCameraList();
		if (camera_infos_.isEmpty())
		{
			panel_->setStatusText(tr("No cameras found"), true);
			panel_->enableCheckBox()->blockSignals(true);
			panel_->enableCheckBox()->setChecked(false);
			panel_->enableCheckBox()->blockSignals(false);
			return;
		}
		startCamera();
	}
	else
		stopCamera();
}

void WebcamWindow::onCameraIndexChanged(int)
{
	if (!panel_->enableCheckBox()->isChecked())
		return;
	stopCamera();
	startCamera();
}

QString WebcamWindow::getWebcamSaveDirectory() const
{
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

	QString path = getWebcamSaveDirectory();
	QString file = QDir(path).absoluteFilePath("metasiberia_webcam_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".jpg");
	applyPhotoEncodingSettings();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	int id = image_capture_->captureToFile(file);
	if (id < 0)
	{
		panel_->setStatusText(tr("Photo failed to start"), true);
		QMessageBox::warning(this, tr("Webcam"), tr("Could not start photo capture."));
		return;
	}
	panel_->setStatusText(tr("Saving..."), false);
#else
	if (!pending_photo_path_.isEmpty())
		return;

	camera_->setCaptureMode(QCamera::CaptureStillImage);
	if (image_capture_->isReadyForCapture())
	{
		next_photo_save_path_ = file;
		camera_->searchAndLock();
		int id = image_capture_->capture(QString());
		camera_->unlock();
		if (id < 0)
		{
			next_photo_save_path_.clear();
			camera_->setCaptureMode(QCamera::CaptureViewfinder);
			panel_->setStatusText(tr("Photo failed to start"), true);
			QMessageBox::warning(this, tr("Webcam"), tr("Could not start photo capture."));
			return;
		}
		panel_->setStatusText(tr("Saving..."), false);
	}
	else
	{
		pending_photo_path_ = file;
		connect(image_capture_.data(), &QCameraImageCapture::readyForCaptureChanged, this, &WebcamWindow::onReadyForCapture);
		panel_->setStatusText(tr("Preparing..."), false);
	}
#endif
}

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
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
	if (id < 0)
	{
		next_photo_save_path_.clear();
		camera_->setCaptureMode(QCamera::CaptureViewfinder);
		panel_->setStatusText(tr("Photo failed to start"), true);
		QMessageBox::warning(this, tr("Webcam"), tr("Could not start photo capture."));
		return;
	}
	panel_->setStatusText(tr("Saving..."), false);
}
#endif

void WebcamWindow::onImageCaptured(int, const QImage& image)
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	Q_UNUSED(image);
#else
	if (next_photo_save_path_.isEmpty() || !camera_)
		return;

	QString path = next_photo_save_path_;
	next_photo_save_path_.clear();
	camera_->setCaptureMode(QCamera::CaptureViewfinder);
	if (image.isNull())
	{
		panel_->setStatusText(tr("Photo failed"), true);
		QMessageBox::warning(this, tr("Webcam"), tr("Photo could not be saved. No image data."));
		return;
	}
	int quality = qBound(1, photo_quality_, 100);
	if (!image.save(path, "JPEG", quality))
	{
		panel_->setStatusText(tr("Photo failed"), true);
		QMessageBox::warning(this, tr("Webcam"), tr("Could not save image to file:\n%1").arg(path));
		return;
	}
	panel_->setStatusText(tr("Saved: %1").arg(QFileInfo(path).fileName()), false);
	panel_->statusLabel()->setToolTip(tr("Saved to: %1").arg(path));
	QTimer::singleShot(4000, this, [this]() { updateStatusLabel(); panel_->statusLabel()->setToolTip(QString()); });
#endif
}

void WebcamWindow::onImageSaved(int, const QString& path)
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
	if (camera_)
		camera_->setCaptureMode(QCamera::CaptureViewfinder);
#endif

	if (path.isEmpty())
		return;

	panel_->setStatusText(tr("Saved: %1").arg(QFileInfo(path).fileName()), false);
	panel_->statusLabel()->setToolTip(tr("Saved to: %1").arg(path));
	QTimer::singleShot(4000, this, [this]() { updateStatusLabel(); panel_->statusLabel()->setToolTip(QString()); });
}

void WebcamWindow::onImageCaptureError(int, int, const QString& errorString)
{
	next_photo_save_path_.clear();
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
	if (camera_)
		camera_->setCaptureMode(QCamera::CaptureViewfinder);
#endif
	panel_->setStatusText(tr("Photo failed"), true);
	QMessageBox::warning(this, tr("Webcam"), tr("Photo could not be saved: %1").arg(errorString.isEmpty() ? tr("Unknown error") : errorString));
}

void WebcamWindow::onRecordClicked()
{
	if (!media_recorder_ || !camera_)
		return;

	QString path = getWebcamSaveDirectory();
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QString file = QDir(path).absoluteFilePath("metasiberia_webcam_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".mp4");
#else
	QString file = QDir(path).absoluteFilePath("metasiberia_webcam_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".wmv");
#endif
	last_video_path_ = file;
	last_recorder_error_.clear();
	applyVideoEncodingSettings();
	media_recorder_->setOutputLocation(QUrl::fromLocalFile(file));
	panel_->setRecordingState(true);
	panel_->setStatusText(tr("Starting..."), false);
	QTimer::singleShot(200, this, [this]() {
		if (media_recorder_)
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

	if (!camera_)
	{
		int index = qMax(0, panel_->cameraComboBox()->currentIndex());
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
		QCamera temp(camera_infos_.at(index));
		const WebcamViewfinderSettingsType current_settings = viewfinder_settings_;
#else
		QCamera temp(camera_infos_.at(index));
		temp.load();
		const WebcamViewfinderSettingsType current_settings = viewfinder_settings_;
#endif
		WebcamSettingsDialog dialog(&temp, current_settings, photo_quality_, video_bitrate_mbps_, this);
		if (dialog.exec() == QDialog::Accepted)
		{
			applyViewfinderSettings(dialog.getViewfinderSettings());
			photo_quality_ = dialog.getPhotoQuality();
			video_bitrate_mbps_ = dialog.getVideoBitrateMbps();
		}
		return;
	}

	WebcamViewfinderSettingsType current_settings = viewfinder_settings_;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	if (!camera_->cameraFormat().isNull())
		current_settings = camera_->cameraFormat();
#endif
	WebcamSettingsDialog dialog(camera_.data(), current_settings, photo_quality_, video_bitrate_mbps_, this);
	connect(&dialog, &WebcamSettingsDialog::viewfinderSettingsChanged, this, [this, &dialog]() {
		applyViewfinderSettings(dialog.getViewfinderSettings());
		updatePreviewInfoLabel();
	});

	if (dialog.exec() == QDialog::Accepted)
	{
		applyViewfinderSettings(dialog.getViewfinderSettings());
		photo_quality_ = dialog.getPhotoQuality();
		video_bitrate_mbps_ = dialog.getVideoBitrateMbps();
		applyPhotoEncodingSettings();
		applyVideoEncodingSettings();
		updatePreviewInfoLabel();
	}
}

void WebcamWindow::onRecorderStateChanged()
{
	if (!media_recorder_)
		return;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QMediaRecorder::RecorderState state = media_recorder_->recorderState();
#else
	QMediaRecorder::State state = media_recorder_->state();
#endif

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
				panel_->setStatusText(tr("Saved: %1").arg(QFileInfo(last_video_path_).fileName()), false);
				panel_->statusLabel()->setToolTip(tr("Saved to: %1").arg(last_video_path_));
				QTimer::singleShot(4000, this, [this]() { updateStatusLabel(); panel_->statusLabel()->setToolTip(QString()); });
			}
			else
			{
				panel_->setStatusText(tr("Recording failed"), true);
			}
			last_video_path_.clear();
		}
		updateStatusLabel();
	}
}

void WebcamWindow::onRecorderError()
{
	recording_ = false;
	panel_->setRecordingState(false);
	last_recorder_error_ = media_recorder_ ? media_recorder_->errorString() : QString();
	last_video_path_.clear();
	panel_->setStatusText(tr("Recording failed"), true);
	QMessageBox::warning(this, tr("Webcam"), tr("Video recording error: %1").arg(last_recorder_error_.isEmpty() ? tr("Unknown error") : last_recorder_error_));
	updateStatusLabel();
}

void WebcamWindow::onCameraStateChanged()
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
	if (pending_start_ && camera_ && camera_->state() == QCamera::LoadedState)
	{
		pending_start_ = false;
		if (viewfinder_settings_.resolution().isValid())
			camera_->setViewfinderSettings(viewfinder_settings_);
		camera_->start();
	}
#endif
	updateStatusLabel();
}

void WebcamWindow::onCameraError()
{
	if (!camera_)
		return;

	QString error_text = camera_->errorString();
	if (!error_text.isEmpty())
	{
		panel_->setStatusText(error_text, true);
		QMessageBox::warning(this, tr("Webcam"), tr("Camera error: %1").arg(error_text));
	}
	else
	{
		panel_->setStatusText(tr("Camera error"), true);
	}

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	viewfinder_settings_ = QCameraFormat();
#else
	viewfinder_settings_ = QCameraViewfinderSettings();
#endif
}

