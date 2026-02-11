/*=====================================================================
WebcamVideoView.h
-----------------
Copyright Glare Technologies Limited 2024 -
Central video area with placeholder overlay when disabled.
=====================================================================*/
#pragma once

#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QCamera;
class QResizeEvent;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
class QVideoWidget;
#else
class QCameraViewfinder;
#endif
QT_END_NAMESPACE

class WebcamVideoView : public QWidget
{
	Q_OBJECT
public:
	explicit WebcamVideoView(QWidget* parent = nullptr);
	~WebcamVideoView();

	void setCamera(QCamera* camera);
	void setPlaceholderVisible(bool visible);
	bool isPlaceholderVisible() const { return placeholder_visible; }

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QVideoWidget* videoWidget() const { return video_widget_; }
#else
	QCameraViewfinder* viewfinder() const { return viewfinder_; }
#endif

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	void updateOverlayVisibility();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QVideoWidget* video_widget_;
#else
	QCameraViewfinder* viewfinder_;
#endif
	QLabel* placeholder_label_;
	QCamera* camera_;
	bool placeholder_visible;
};
