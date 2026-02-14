/*=====================================================================
WebcamSettingsDialog.cpp
-----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "webcam/WebcamSettingsDialog.h"

#include <QtMultimedia/QCamera>
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtMultimedia/QCameraDevice>
#include <QtCore/QSet>
#include <algorithm>
#endif
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QGroupBox>

WebcamSettingsDialog::WebcamSettingsDialog(
	QCamera* camera,
	const WebcamDialogViewfinderSettingsType& current,
	int photo_quality,
	qreal video_bitrate_mbps,
	QWidget* parent)
	: QDialog(parent)
	, camera_(camera)
	, resolution_combo_(nullptr)
	, fps_combo_(nullptr)
	, photo_quality_slider_(nullptr)
	, photo_quality_label_(nullptr)
	, video_bitrate_spin_(nullptr)
{
	setWindowTitle(tr("Webcam Settings"));
	QVBoxLayout* main = new QVBoxLayout(this);

	QGroupBox* preview_group = new QGroupBox(tr("Preview"), this);
	QFormLayout* preview_form = new QFormLayout(preview_group);
	resolution_combo_ = new QComboBox(this);
	preview_form->addRow(tr("Resolution:"), resolution_combo_);
	fps_combo_ = new QComboBox(this);
	preview_form->addRow(tr("Frame rate:"), fps_combo_);
	main->addWidget(preview_group);

	QGroupBox* photo_group = new QGroupBox(tr("Photo"), this);
	QFormLayout* photo_form = new QFormLayout(photo_group);
	photo_quality_slider_ = new QSlider(Qt::Horizontal, this);
	photo_quality_slider_->setRange(1, 100);
	photo_quality_slider_->setValue(photo_quality);
	photo_quality_label_ = new QLabel(this);
	photo_form->addRow(tr("JPEG quality:"), photo_quality_slider_);
	photo_form->addRow(QString(), photo_quality_label_);
	connect(photo_quality_slider_, &QSlider::valueChanged, this, [this](int value) {
		photo_quality_label_->setText(QString::number(value) + "%");
	});
	photo_quality_label_->setText(QString::number(photo_quality_slider_->value()) + "%");
	main->addWidget(photo_group);

	QGroupBox* video_group = new QGroupBox(tr("Video"), this);
	QFormLayout* video_form = new QFormLayout(video_group);
	video_bitrate_spin_ = new QDoubleSpinBox(this);
	video_bitrate_spin_->setRange(0.5, 20.0);
	video_bitrate_spin_->setSingleStep(0.5);
	video_bitrate_spin_->setSuffix(tr(" Mbps"));
	video_bitrate_spin_->setValue(video_bitrate_mbps);
	video_form->addRow(tr("Bitrate:"), video_bitrate_spin_);
	main->addWidget(video_group);

	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	main->addWidget(buttons);

	populateFromCamera();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	if (!current.isNull())
	{
		const QSize current_res = current.resolution();
		const int current_fps = static_cast<int>(qRound(current.maxFrameRate()));
		for (int i = 0; i < resolution_combo_->count(); ++i)
		{
			if (resolution_combo_->itemData(i).toSize() == current_res)
			{
				resolution_combo_->setCurrentIndex(i);
				break;
			}
		}
		for (int i = 0; i < fps_combo_->count(); ++i)
		{
			if (fps_combo_->itemData(i).toInt() == current_fps)
			{
				fps_combo_->setCurrentIndex(i);
				break;
			}
		}
	}

	connect(resolution_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		refreshFpsForCurrentResolution();
		emit viewfinderSettingsChanged();
	});
#else
	QSize cur = current.resolution();
	qreal minF = current.minimumFrameRate();
	qreal maxF = current.maximumFrameRate();
	for (int i = 0; i < resolution_combo_->count(); ++i)
	{
		if (resolution_combo_->itemData(i).toSize() == cur)
		{
			resolution_combo_->setCurrentIndex(i);
			break;
		}
	}
	for (int i = 0; i < fps_combo_->count() && i < fps_ranges_.size(); ++i)
	{
		const QPair<qreal, qreal>& range = fps_ranges_[i];
		if (qFuzzyCompare(range.first, minF) && qFuzzyCompare(range.second, maxF))
		{
			fps_combo_->setCurrentIndex(i);
			break;
		}
	}

	connect(resolution_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WebcamSettingsDialog::viewfinderSettingsChanged);
#endif

	connect(fps_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WebcamSettingsDialog::viewfinderSettingsChanged);
}

WebcamSettingsDialog::~WebcamSettingsDialog()
{}

void WebcamSettingsDialog::populateFromCamera()
{
	if (!camera_)
		return;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	camera_formats_ = camera_->cameraDevice().videoFormats();
	if (camera_formats_.isEmpty())
		return;

	QSet<QString> seen_resolutions;
	QList<QSize> unique_resolutions;
	for (const QCameraFormat& format : camera_formats_)
	{
		if (!format.isNull() && format.resolution().isValid())
		{
			const QSize res = format.resolution();
			const QString key = QString::number(res.width()) + "x" + QString::number(res.height());
			if (!seen_resolutions.contains(key))
			{
				seen_resolutions.insert(key);
				unique_resolutions.append(res);
			}
		}
	}

	std::sort(unique_resolutions.begin(), unique_resolutions.end(), [](const QSize& a, const QSize& b) {
		const int area_a = a.width() * a.height();
		const int area_b = b.width() * b.height();
		if (area_a != area_b)
			return area_a > area_b;
		return a.width() > b.width();
	});

	resolution_combo_->clear();
	for (const QSize& res : unique_resolutions)
		resolution_combo_->addItem(QString("%1 x %2").arg(res.width()).arg(res.height()), res);

	refreshFpsForCurrentResolution();
#else
	QList<QSize> resolutions = camera_->supportedViewfinderResolutions();
	if (!resolutions.isEmpty())
	{
		resolution_combo_->clear();
		resolutions_ = resolutions;
		for (const QSize& size : resolutions_)
			resolution_combo_->addItem(QString("%1 x %2").arg(size.width()).arg(size.height()), size);
	}

	QList<QCamera::FrameRateRange> ranges = camera_->supportedViewfinderFrameRateRanges();
	if (!ranges.isEmpty())
	{
		fps_combo_->clear();
		fps_ranges_.clear();
		for (const QCamera::FrameRateRange& range : ranges)
		{
			fps_ranges_.append(qMakePair(range.minimumFrameRate, range.maximumFrameRate));
			fps_combo_->addItem(QString("%1 - %2 FPS").arg(range.minimumFrameRate).arg(range.maximumFrameRate));
		}
	}
#endif
}

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
void WebcamSettingsDialog::refreshFpsForCurrentResolution()
{
	fps_combo_->clear();
	if (resolution_combo_->currentIndex() < 0)
		return;

	const QSize selected_res = resolution_combo_->currentData().toSize();
	QSet<int> fps_set;
	QList<int> fps_values;

	for (const QCameraFormat& format : camera_formats_)
	{
		if (format.resolution() == selected_res)
		{
			const int fps = static_cast<int>(qRound(format.maxFrameRate()));
			if (fps > 0 && !fps_set.contains(fps))
			{
				fps_set.insert(fps);
				fps_values.append(fps);
			}
		}
	}

	std::sort(fps_values.begin(), fps_values.end());
	for (int fps : fps_values)
		fps_combo_->addItem(QString::number(fps) + tr(" FPS"), fps);
}
#endif

WebcamDialogViewfinderSettingsType WebcamSettingsDialog::getViewfinderSettings() const
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	if (resolution_combo_->currentIndex() < 0)
		return QCameraFormat();

	const QSize selected_res = resolution_combo_->currentData().toSize();
	const int selected_fps = fps_combo_->currentData().toInt();

	QCameraFormat best_match;
	for (const QCameraFormat& format : camera_formats_)
	{
		if (format.resolution() != selected_res)
			continue;

		const int fps = static_cast<int>(qRound(format.maxFrameRate()));
		if (selected_fps <= 0 || fps == selected_fps)
			return format;
		if (best_match.isNull())
			best_match = format;
	}

	return best_match;
#else
	QCameraViewfinderSettings settings;
	if (resolution_combo_->currentIndex() >= 0 && resolution_combo_->currentData().isValid())
		settings.setResolution(resolution_combo_->currentData().toSize());

	const int fps_index = fps_combo_->currentIndex();
	if (fps_index >= 0 && fps_index < fps_ranges_.size())
	{
		const QPair<qreal, qreal>& range = fps_ranges_[fps_index];
		settings.setMinimumFrameRate(range.first);
		settings.setMaximumFrameRate(range.second);
	}

	return settings;
#endif
}

int WebcamSettingsDialog::getPhotoQuality() const
{
	return photo_quality_slider_ ? photo_quality_slider_->value() : 85;
}

qreal WebcamSettingsDialog::getVideoBitrateMbps() const
{
	return video_bitrate_spin_ ? video_bitrate_spin_->value() : 5.0;
}

