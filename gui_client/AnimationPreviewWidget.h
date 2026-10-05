#pragma once

#include "../shared/URLString.h"
#include <QtWidgets/QWidget>
#include <functional>
#include <memory>
#include <string>

class GUIClient;
class QSettings;

// Qt-only, GUI-thread preview. Construction/setters do not initialise GL while hidden.
// The owner must call shutdownGL() BEFORE destroying the main GL context/GUIClient.
class AnimationPreviewWidget : public QWidget
{
	Q_OBJECT
public:
	AnimationPreviewWidget(const std::string& base_dir_path, QSettings* settings, GUIClient* gui_client,
		std::function<void()> restoreMainContext, QWidget* parent = nullptr);
	~AnimationPreviewWidget() override;

	void setAnimation(URLString url); // A locally available .subanim resource; empty clears selection.
	void setPlaying(bool playing);
	void seek(double normalized); // Clamped to [0, 1]; does not change playback state.
	void step(int frames); // Pauses, then moves by frames / 30 seconds, clamped at either end.
	void setSpeed(double speed); // Positive multiplier, clamped to [0.05, 8].
	void setLoop(bool loop);
	void shutdownGL(); // Idempotent and final; releases resources and forgets the context callback.

signals:
	void positionChanged(double normalized);
	void durationChanged(double seconds);
	void playbackChanged(bool playing);
	void statusChanged(QString status);

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
