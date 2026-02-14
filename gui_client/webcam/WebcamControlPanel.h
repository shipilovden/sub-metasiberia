/*=====================================================================
WebcamControlPanel.h
--------------------
Copyright Glare Technologies Limited 2024 -
Bottom control bar: Enable Webcam, camera selection, Photo, Record, Stop, Settings.
=====================================================================*/
#pragma once

#include <QtWidgets/QWidget>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>

/*=====================================================================
WebcamControlPanel
------------------
Bottom panel (~50–60 px): checkbox, camera combo, action buttons, status.
=====================================================================*/
class WebcamControlPanel : public QWidget
{
	Q_OBJECT
public:
	explicit WebcamControlPanel(QWidget* parent = nullptr);
	~WebcamControlPanel();

	QCheckBox* enableCheckBox() const { return enable_checkbox_; }
	QComboBox* cameraComboBox() const { return camera_combo_; }
	QPushButton* photoButton() const { return photo_btn_; }
	QPushButton* recordButton() const { return record_btn_; }
	QPushButton* stopButton() const { return stop_btn_; }
	QPushButton* openFolderButton() const { return open_folder_btn_; }
	QPushButton* settingsButton() const { return settings_btn_; }
	QLabel* statusLabel() const { return status_label_; }

	// Status text: e.g. "• Active" (green), "• Recording…" (red).
	void setStatusText(const QString& text, bool recording);

public slots:
	void setRecordingState(bool recording);
	void setControlsEnabled(bool camera_available);

private:
	void setupStyle();

	QCheckBox* enable_checkbox_;
	QComboBox* camera_combo_;
	QPushButton* photo_btn_;
	QPushButton* record_btn_;
	QPushButton* stop_btn_;
	QPushButton* open_folder_btn_;
	QPushButton* settings_btn_;
	QLabel* status_label_;
};
