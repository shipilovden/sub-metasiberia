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
class QVideoWidget;
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

	QVideoWidget* videoWidget() const { return video_widget_; }

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	void updateOverlayVisibility();

	QVideoWidget* video_widget_;
	QLabel* placeholder_label_;
	QCamera* camera_;
	bool placeholder_visible;
};
