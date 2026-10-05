#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QVector>

class QSettings;
class QLabel;
class QLineEdit;
class QTableWidget;
class QCheckBox;
class QDoubleSpinBox;
class QSlider;
class QPushButton;
class QVBoxLayout;

struct AnimationEditorItem
{
	QString id; // Resource URL, not the display name.
	QString name;
	double duration_seconds = 0;
	bool loop = false;
	bool animate_head = false;
};

// Presentation only. The Qt adapter connects this panel to the existing gesture pipeline.
class AnimationEditorPanel final : public QWidget
{
	Q_OBJECT
public:
	explicit AnimationEditorPanel(QSettings* settings, QWidget* parent = nullptr);
	void setIconDirectory(const QString& directory);
	void setAnimations(const QVector<AnimationEditorItem>& items, const QString& select_id = QString());
	void setPreviewWidget(QWidget* widget);
	void setPreviewDuration(double seconds);
	void setPreviewPosition(double normalised_position);
	void setPreviewPlaying(bool playing);
	void setPreviewStatus(const QString& status);
	void setEditingEnabled(bool enabled);
	QString selectedAnimationId() const;
	void setBusy(bool busy);

signals:
	void animationSelected(const QString& id);
	void previewPlaybackChanged(bool playing);
	void previewSeekRequested(double position);
	void previewStepRequested(int direction);
	void previewSpeedChanged(double speed);
	void previewLoopChanged(bool loop);
	void importRequested();
	void saveCopyRequested(const QString& id, const QString& name, double speed);
	void flagsEdited(const QString& id, bool loop, bool animate_head);
	void removeRequested(const QString& id);
	void performRequested(const QString& id);
	void stopRequested();
	void exportRequested(const QString& id);

private:
	void selectionChanged();
	void updateTime();
	void updateActions();
	QVector<AnimationEditorItem> animations;
	QString icon_directory;
	QVBoxLayout* preview_layout;
	QLabel* status_label;
	QLabel* time_label;
	QLineEdit* search;
	QLineEdit* copy_name;
	QTableWidget* table;
	QSlider* timeline;
	QCheckBox* loop;
	QCheckBox* animate_head;
	QDoubleSpinBox* speed;
	QPushButton* play;
	QPushButton* previous;
	QPushButton* next;
	QPushButton* rewind;
	QPushButton* import_button;
	QPushButton* save_copy;
	QPushButton* remove_button;
	QPushButton* perform_button;
	QPushButton* stop_button;
	QPushButton* export_button;
	double duration = 0;
	bool can_edit = false;
	bool busy = false;
};
