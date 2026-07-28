/*=====================================================================
AnimationEditorPanel.h
----------------------
Native Qt animation-profile editor panel for Metasiberia.
=====================================================================*/
#pragma once


#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>


class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSettings;
class QSlider;
class QTableWidget;
class QTabWidget;
class QToolButton;


struct AnimationEditorItem
{
	QString id;
	QString name;
	QString category;
	QString source;
	double duration_seconds = 0.0;
	bool favourite = false;
};


class AnimationEditorPanel final : public QWidget
{
	Q_OBJECT

public:
	explicit AnimationEditorPanel(QSettings* settings, QWidget* parent = nullptr);

	void setIconDirectory(const QString& directory);
	void setAnimations(const QVector<AnimationEditorItem>& animations);
	void setPreviewDuration(double duration_seconds);
	void setPreviewPosition(double normalised_position);
	void setPreviewStatus(const QString& status);
	QVariantMap currentProfileState() const;
	QString currentProfileName() const;

signals:
	void animationSelected(const QString& animation_id);
	void previewPlaybackChanged(bool playing);
	void previewSeekRequested(double normalised_position);
	void previewStepRequested(int direction);
	void previewResetRequested();
	void profileChanged(const QString& profile_name, const QVariantMap& state);
	void profileSaved(const QString& profile_name, const QVariantMap& state);
	void applyProfileRequested(const QString& profile_name, const QVariantMap& state);
	void animationImportRequested(const QString& filename, const QString& format);
	void addAnimationRequested();
	void settingsChanged(const QVariantMap& state);

private:
	QWidget* makeLibraryTab();
	QWidget* makeSettingsTab();
	QWidget* makeAssignmentTab();
	QWidget* makeTransitionsTab();
	QWidget* makeEventsTab();
	QWidget* makeSkeletonTab();
	QWidget* makeImportTab();
	void refreshAnimationTable();
	void refreshAnimationCombos();
	void refreshProfiles();
	void loadProfile(const QString& profile_name);
	void saveCurrentProfile(bool emit_signal);
	void controlsChanged();
	void pushUndoSnapshot();
	void undo();
	void redo();
	void restoreState(const QVariantMap& state);
	QVariantMap captureState() const;
	QString selectedAnimationId() const;
	void updateUndoButtons();
	void updateTransportText();
	void applyIcons();

	QSettings* settings;
	QString icon_directory;
	QVector<AnimationEditorItem> animations;
	QVariantMap deferred_profile_state;
	QVector<QVariantMap> undo_stack;
	QVector<QVariantMap> redo_stack;
	bool restoring_state;
	double preview_duration_seconds;

	QComboBox* profile_combo;
	QToolButton* save_profile_button;
	QToolButton* undo_button;
	QToolButton* redo_button;
	QLabel* preview_label;
	QLabel* preview_time_label;
	QToolButton* previous_button;
	QToolButton* play_button;
	QToolButton* next_button;
	QToolButton* reset_preview_button;
	QSlider* timeline_slider;
	QTabWidget* tabs;
	QLineEdit* search_edit;
	QComboBox* category_combo;
	QListWidget* category_list;
	QTableWidget* animation_table;
	QCheckBox* loop_check;
	QCheckBox* root_motion_check;
	QCheckBox* mirror_check;
	QCheckBox* interruptible_check;
	QDoubleSpinBox* speed_spin;
	QDoubleSpinBox* blend_in_spin;
	QDoubleSpinBox* blend_out_spin;
	QDoubleSpinBox* transition_duration_spin;
	QTableWidget* assignment_table;
	QTableWidget* events_table;
	QTableWidget* skeleton_table;
	QLineEdit* import_filename_edit;
	QComboBox* import_format_combo;
	QPushButton* import_button;
	QPushButton* add_button;
	QPushButton* apply_button;
	QPushButton* save_set_button;
};
