/*=====================================================================
PhotoVideoSettingsPanel.h
-------------------------
Native Qt photo and video settings panel for Metasiberia.
=====================================================================*/
#pragma once


#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtWidgets/QWidget>


class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSettings;
class QSpinBox;
class QTabWidget;
class QToolButton;


class PhotoVideoSettingsPanel final : public QWidget
{
	Q_OBJECT

public:
	explicit PhotoVideoSettingsPanel(QSettings* settings, QWidget* parent = nullptr);

	void setIconDirectory(const QString& directory);
	void setRecording(bool recording);
	QVariantMap currentSettings() const;
	QString currentPresetName() const;

signals:
	void settingsChanged(const QVariantMap& settings);
	void cameraModeChanged(const QString& camera_mode);
	void autofocusModeChanged(const QString& autofocus_mode);
	void capturePhotoRequested(const QVariantMap& settings);
	void recordingChanged(bool recording, const QVariantMap& settings);
	void resetRequested(const QVariantMap& settings);
	void browseGalleryRequested();
	void outputDirectoryBrowseRequested();
	void presetSaved(const QString& preset_name, const QVariantMap& settings);

private:
	QWidget* makeCameraTab();
	QWidget* makeVideoTab();
	QWidget* makeOutputTab();
	QWidget* makeSliderRow(const QString& label, QDoubleSpinBox*& spin,
		double minimum, double maximum, double step, int decimals, const QString& suffix);
	void refreshPresets();
	void loadPreset(const QString& preset_name);
	void saveCurrentPreset();
	void deleteCurrentPreset();
	void restoreState(const QVariantMap& state);
	QVariantMap captureState() const;
	void controlsChanged();
	void resetControls();
	void applyIcons();
	void updateRecordButton();

	QSettings* settings;
	QString icon_directory;
	bool restoring_state;
	QTimer settings_save_timer;

	QComboBox* preset_combo;
	QToolButton* save_preset_button;
	QToolButton* delete_preset_button;
	QTabWidget* tabs;
	QComboBox* camera_mode_combo;
	QComboBox* autofocus_mode_combo;
	QDoubleSpinBox* dof_blur_spin;
	QDoubleSpinBox* focus_distance_spin;
	QDoubleSpinBox* ev_spin;
	QDoubleSpinBox* saturation_spin;
	QDoubleSpinBox* focal_length_spin;
	QDoubleSpinBox* roll_spin;
	QCheckBox* grid_check;
	QCheckBox* hide_ui_check;
	QComboBox* resolution_combo;
	QSpinBox* frame_rate_spin;
	QComboBox* codec_combo;
	QSpinBox* bitrate_spin;
	QComboBox* quality_combo;
	QCheckBox* stabilisation_check;
	QCheckBox* microphone_check;
	QCheckBox* system_audio_check;
	QSpinBox* maximum_duration_spin;
	QComboBox* image_format_combo;
	QComboBox* colour_space_combo;
	QLineEdit* output_directory_edit;
	QCheckBox* timestamp_check;
	QCheckBox* metadata_check;
	QPushButton* reset_button;
	QPushButton* gallery_button;
	QPushButton* capture_button;
	QPushButton* record_button;
};
