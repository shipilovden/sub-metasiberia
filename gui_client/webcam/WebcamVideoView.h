/*=====================================================================
WebcamVideoView.h
-----------------
Copyright Glare Technologies Limited 2024 -
Central video area: QCameraViewfinder + placeholder overlay when disabled.
=====================================================================*/
#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QScopedPointer>

QT_BEGIN_NAMESPACE
class QCameraViewfinder;
class QLabel;
class QCamera;
class QResizeEvent;
QT_END_NAMESPACE

/*=====================================================================
WebcamVideoView
---------------
Widget that occupies 90–95% of the window: shows live video from
QCameraViewfinder or a centered placeholder when the camera is disabled.
=====================================================================*/
class WebcamVideoView : public QWidget
{
	Q_OBJECT
public:
	explicit WebcamVideoView(QWidget* parent = nullptr);
	~WebcamVideoView();

	// Set the camera to display; pass nullptr to show placeholder only.
	void setCamera(QCamera* camera);

	// Show/hide the placeholder text (e.g. when camera is disabled).
	void setPlaceholderVisible(bool visible);
	bool isPlaceholderVisible() const { return placeholder_visible; }

	QCameraViewfinder* viewfinder() const { return viewfinder_; }

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	void updateOverlayVisibility();

	QCameraViewfinder* viewfinder_;
	QLabel* placeholder_label_;
	QCamera* camera_;
	bool placeholder_visible;
};
