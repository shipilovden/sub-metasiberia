/*=====================================================================
WebcamSettingsDialog.cpp
-----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "webcam/WebcamSettingsDialog.h"
#include <QtMultimedia/QCamera>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QGroupBox>
#include <QtCore/QPair>

WebcamSettingsDialog::WebcamSettingsDialog(QCamera* camera, const QCameraViewfinderSettings& current,
	int photo_quality, qreal video_bitrate_mbps, QWidget* parent)
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
	connect(photo_quality_slider_, &QSlider::valueChanged, this, [this](int v) {
		photo_quality_label_->setText(QString::number(v) + "%");
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

	// Restore selection to current viewfinder
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
		const QPair<qreal,qreal>& r = fps_ranges_[i];
		if (qFuzzyCompare(r.first, minF) && qFuzzyCompare(r.second, maxF))
		{
			fps_combo_->setCurrentIndex(i);
			break;
		}
	}

	// Live apply: when user changes resolution or FPS, emit so preview updates immediately
	connect(resolution_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WebcamSettingsDialog::viewfinderSettingsChanged);
	connect(fps_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WebcamSettingsDialog::viewfinderSettingsChanged);
}

WebcamSettingsDialog::~WebcamSettingsDialog()
{}

void WebcamSettingsDialog::populateFromCamera()
{
	if (!camera_)
		return;
	// Camera should be in Loaded or Active state to query supported settings
	QList<QSize> res = camera_->supportedViewfinderResolutions();
	if (!res.isEmpty())
	{
		resolution_combo_->clear();
		resolutions_ = res;
		for (const QSize& s : res)
			resolution_combo_->addItem(QString("%1 x %2").arg(s.width()).arg(s.height()), s);
	}
	QList<QCamera::FrameRateRange> ranges = camera_->supportedViewfinderFrameRateRanges();
	if (!ranges.isEmpty())
	{
		fps_combo_->clear();
		fps_ranges_.clear();
		for (const QCamera::FrameRateRange& r : ranges)
		{
			fps_ranges_.append(qMakePair(r.minimumFrameRate, r.maximumFrameRate));
			fps_combo_->addItem(QString("%1 - %2 FPS").arg(r.minimumFrameRate).arg(r.maximumFrameRate));
		}
	}
}

QCameraViewfinderSettings WebcamSettingsDialog::getViewfinderSettings() const
{
	QCameraViewfinderSettings s;
	if (resolution_combo_->currentIndex() >= 0 && resolution_combo_->currentData().isValid())
		s.setResolution(resolution_combo_->currentData().toSize());
	int fi = fps_combo_->currentIndex();
	if (fi >= 0 && fi < fps_ranges_.size())
	{
		const QPair<qreal,qreal>& r = fps_ranges_[fi];
		s.setMinimumFrameRate(r.first);
		s.setMaximumFrameRate(r.second);
	}
	return s;
}

int WebcamSettingsDialog::getPhotoQuality() const
{
	return photo_quality_slider_ ? photo_quality_slider_->value() : 85;
}

qreal WebcamSettingsDialog::getVideoBitrateMbps() const
{
	return video_bitrate_spin_ ? video_bitrate_spin_->value() : 5.0;
}
