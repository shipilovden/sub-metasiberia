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
class QImage;
class QLabel;
class QPushButton;
class QSettings;
class QSpinBox;
class QStackedWidget;
class QToolButton;


class PhotoVideoSettingsPanel final : public QWidget
{
	Q_OBJECT

public:
	explicit PhotoVideoSettingsPanel(QSettings* settings, QWidget* parent = nullptr);
	~PhotoVideoSettingsPanel() override;

	void setIconDirectory(const QString& directory);
	void setRecording(bool recording);
	void setRecordingFinalising();
	QVariantMap currentSettings() const;
	QString currentPresetName() const;
	void cancelCapture();
	void updateAutofocusDistance(double distance);
	void setManualFocusDistance(double distance);
	void setCameraMode(const QString& mode);
	void setPathOptics(double focus, double lens_mm, double roll_degrees);
	void setHistogram(const QImage& image);
	void setTrajectoryStatus(const QString& status);

signals:
	void settingsChanged(const QVariantMap& settings);
	void cameraModeChanged(const QString& camera_mode);
	void autofocusModeChanged(const QString& autofocus_mode);
	void capturePhotoRequested(const QVariantMap& settings);
	void recordingChanged(bool recording, const QVariantMap& settings);
	void resetRequested(const QVariantMap& settings);
	void browseGalleryRequested();
	void uploadPhotoRequested();
	void outputDirectoryBrowseRequested();
	void presetSaved(const QString& preset_name, const QVariantMap& settings);
	void focusPickRequested();
	// Actions: add, remove (last keyframe), clear, play, stop. MainWindow owns the poses.
	void trajectoryActionRequested(const QString& action);

private:
	QWidget* makeCameraTab();
	QWidget* makeVideoTab();
	QWidget* makeOutputTab();
	QWidget* makeSliderRow(const QString& label, QDoubleSpinBox*& spin,
		double minimum, double maximum, double step, int decimals, const QString& suffix, double default_value = 0.0);
	QWidget* makeProSlider(const QString& key, const QString& label, double minimum, double maximum, double default_value, double step = 0.01, int decimals = 2);
	QCheckBox* makeProCheck(const QString& key, const QString& label);
	void setCompareOriginal(bool enabled);
	bool eventFilter(QObject* watched, QEvent* event) override;
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
	QTimer capture_timer;
	int capture_seconds_remaining = 0;

	QComboBox* preset_combo;
	QToolButton* save_preset_button;
	QToolButton* delete_preset_button;
	QStackedWidget* tabs;
	QWidget* colour_page = nullptr;
	QWidget* effects_page = nullptr;
	QWidget* composition_page = nullptr;
	QWidget* video_page = nullptr;
	QMap<QString, QDoubleSpinBox*> pro_sliders;
	QMap<QString, QCheckBox*> pro_checks;
	QVariantMap pro_defaults;
	QPushButton* compare_button = nullptr;
	bool compare_original = false;
	QLabel* histogram_label = nullptr;
	QLabel* trajectory_status = nullptr;
	QLineEdit* lut_path_edit = nullptr;
	QComboBox* photo_resolution_combo = nullptr;
	QComboBox* camera_mode_combo;
	QComboBox* autofocus_mode_combo;
	QDoubleSpinBox* dof_blur_spin;
	QDoubleSpinBox* focus_distance_spin;
	QDoubleSpinBox* ev_spin;
	QDoubleSpinBox* saturation_spin;
	QDoubleSpinBox* focal_length_spin;
	QDoubleSpinBox* roll_spin;
	QDoubleSpinBox* bloom_spin = nullptr;
	QDoubleSpinBox* warmth_spin = nullptr;
	QDoubleSpinBox* tint_spin = nullptr;
	QDoubleSpinBox* vignette_spin = nullptr;
	QDoubleSpinBox* shift_x_spin = nullptr;
	QDoubleSpinBox* shift_y_spin = nullptr;
	QCheckBox* grid_check;
	QCheckBox* hide_ui_check;
	QComboBox* aspect_ratio_combo;
	QComboBox* capture_delay_combo;
	QSpinBox* image_quality_spin;
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
