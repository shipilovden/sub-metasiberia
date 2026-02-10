/*=====================================================================
WebcamVideoView.cpp
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "webcam/WebcamVideoView.h"
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QFrame>
#include <QtGui/QResizeEvent>
#include <QtMultimediaWidgets/QCameraViewfinder>
#include <QtMultimedia/QCamera>

WebcamVideoView::WebcamVideoView(QWidget* parent)
	: QWidget(parent)
	, viewfinder_(nullptr)
	, placeholder_label_(nullptr)
	, camera_(nullptr)
	, placeholder_visible(true)
{
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	viewfinder_ = new QCameraViewfinder(this);
	viewfinder_->setMinimumSize(320, 240);
	viewfinder_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	layout->addWidget(viewfinder_, 1);

	placeholder_label_ = new QLabel(this);
	placeholder_label_->setText(tr("Webcam feed will appear here"));
	placeholder_label_->setAlignment(Qt::AlignCenter);
	placeholder_label_->setStyleSheet("color: gray; font-size: 14px;");
	placeholder_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	placeholder_label_->raise();
	placeholder_label_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
	updateOverlayVisibility();
}

WebcamVideoView::~WebcamVideoView()
{}

void WebcamVideoView::setCamera(QCamera* camera)
{
	if (camera_ == camera)
		return;
	camera_ = camera;
	if (viewfinder_ && camera_)
		camera_->setViewfinder(viewfinder_);
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
	if (placeholder_label_)
	{
		placeholder_label_->setVisible(placeholder_visible);
		placeholder_label_->setGeometry(0, 0, width(), height());
	}
}

void WebcamVideoView::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (placeholder_label_ && placeholder_label_->isVisible())
		placeholder_label_->setGeometry(0, 0, width(), height());
}
