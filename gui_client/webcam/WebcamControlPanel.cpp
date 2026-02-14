/*=====================================================================
WebcamControlPanel.cpp
----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "webcam/WebcamControlPanel.h"
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QStyle>
#include <QtGui/QIcon>

WebcamControlPanel::WebcamControlPanel(QWidget* parent)
	: QWidget(parent)
{
	enable_checkbox_ = new QCheckBox(this);
	enable_checkbox_->setText(tr("Enable Webcam"));
	enable_checkbox_->setObjectName("webcamEnableCheckBox");

	camera_combo_ = new QComboBox(this);
	camera_combo_->setObjectName("webcamCameraCombo");
	camera_combo_->setMinimumWidth(180);

	photo_btn_ = new QPushButton(this);
	photo_btn_->setText(tr("Photo"));
	photo_btn_->setObjectName("webcamPhotoButton");
	photo_btn_->setToolTip(tr("Capture a photo"));

	record_btn_ = new QPushButton(this);
	record_btn_->setText(tr("Record"));
	record_btn_->setObjectName("webcamRecordButton");
	record_btn_->setToolTip(tr("Start recording video"));

	stop_btn_ = new QPushButton(this);
	stop_btn_->setText(tr("Stop"));
	stop_btn_->setObjectName("webcamStopButton");
	stop_btn_->setToolTip(tr("Stop recording"));
	stop_btn_->setVisible(false);

	open_folder_btn_ = new QPushButton(this);
	open_folder_btn_->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
	open_folder_btn_->setObjectName("webcamOpenFolderButton");
	open_folder_btn_->setToolTip(tr("Open folder with photos and recordings"));
	open_folder_btn_->setFixedSize(32, 28);

	settings_btn_ = new QPushButton(this);
	settings_btn_->setText(tr("Settings"));
	settings_btn_->setObjectName("webcamSettingsButton");
	settings_btn_->setToolTip(tr("Resolution, FPS, photo and video settings"));

	status_label_ = new QLabel(this);
	status_label_->setObjectName("webcamStatusLabel");
	status_label_->setText(tr("• Inactive"));
	setStatusText(tr("• Inactive"), false);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(8, 6, 8, 6);
	layout->setSpacing(10);

	layout->addWidget(enable_checkbox_);
	layout->addWidget(camera_combo_);
	layout->addSpacing(16);
	layout->addWidget(photo_btn_);
	layout->addWidget(record_btn_);
	layout->addWidget(stop_btn_);
	layout->addWidget(open_folder_btn_);
	layout->addWidget(settings_btn_);
	layout->addStretch(1);
	layout->addWidget(status_label_);

	setFixedHeight(52);
	setupStyle();
}

WebcamControlPanel::~WebcamControlPanel()
{}

void WebcamControlPanel::setupStyle()
{
	// Light style to match the rest of the application (light background, black text)
	setStyleSheet(
		"WebcamControlPanel { background-color: #f0f0f0; } "
		"QCheckBox { color: #000000; } "
		"QComboBox { color: #000000; background-color: #ffffff; min-height: 24px; border: 1px solid #ccc; } "
		"QPushButton { color: #000000; background-color: #e8e8e8; min-height: 24px; padding: 4px 12px; border: 1px solid #ccc; } "
		"QPushButton:hover { background-color: #d8d8d8; } "
		"QPushButton:pressed { background-color: #c0c0c0; } "
		"QPushButton#webcamRecordButton:checked { background-color: #ffcccc; color: #8b0000; } "
		"QLabel#webcamStatusLabel { color: #333333; } "
	);
}

void WebcamControlPanel::setStatusText(const QString& text, bool recording)
{
	status_label_->setText(text);
	if (recording)
		status_label_->setStyleSheet("color: #c44;");
	else
		status_label_->setStyleSheet("color: #228b22;");
}

void WebcamControlPanel::setRecordingState(bool recording)
{
	record_btn_->setVisible(!recording);
	stop_btn_->setVisible(recording);
	record_btn_->setChecked(recording);
}

void WebcamControlPanel::setControlsEnabled(bool camera_available)
{
	photo_btn_->setEnabled(camera_available);
	record_btn_->setEnabled(camera_available);
	stop_btn_->setEnabled(camera_available);
	open_folder_btn_->setEnabled(true);  // always allow opening folder
	settings_btn_->setEnabled(camera_available);
	camera_combo_->setEnabled(camera_available);
}
