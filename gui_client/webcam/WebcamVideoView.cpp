/*=====================================================================
WebcamVideoView.cpp
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "webcam/WebcamVideoView.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <QtGui/QResizeEvent>
#include <QtMultimedia/QCamera>
#include <QtMultimediaWidgets/QVideoWidget>

WebcamVideoView::WebcamVideoView(QWidget* parent)
	: QWidget(parent)
	, video_widget_(nullptr)
	, placeholder_label_(nullptr)
	, camera_(nullptr)
	, placeholder_visible(true)
{
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	video_widget_ = new QVideoWidget(this);
	video_widget_->setMinimumSize(320, 240);
	video_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	layout->addWidget(video_widget_, 1);

	placeholder_label_ = new QLabel(this);
	placeholder_label_->setText(tr("Webcam feed will appear here"));
	placeholder_label_->setAlignment(Qt::AlignCenter);
	placeholder_label_->setStyleSheet("color: gray; font-size: 14px;");
	placeholder_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	placeholder_label_->raise();
	updateOverlayVisibility();
}

WebcamVideoView::~WebcamVideoView()
{}

void WebcamVideoView::setCamera(QCamera* camera)
{
	if (camera_ == camera)
		return;
	camera_ = camera;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
	if (video_widget_ && camera_)
		camera_->setViewfinder(video_widget_);
#endif
}

void WebcamVideoView::setPlaceholderVisible(bool visible)
{
	if (placeholder_visible == visible)
		return;
	placeholder_visible = visible;
	updateOverlayVisibility();
}

void WebcamVideoView::updateOverlayVisibility()
{
	if (!placeholder_label_)
		return;
	placeholder_label_->setVisible(placeholder_visible);
	placeholder_label_->setGeometry(0, 0, width(), height());
}

void WebcamVideoView::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (placeholder_label_ && placeholder_label_->isVisible())
		placeholder_label_->setGeometry(0, 0, width(), height());
}
