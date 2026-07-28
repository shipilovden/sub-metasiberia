/*=====================================================================
ScientificImageViewer.cpp
=====================================================================*/
#include "ScientificImageViewer.h"


#include <QtCore/QFileInfo>
#include <QtCore/QFile>
#include <QtGui/QDesktopServices>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGraphicsPixmapItem>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>


ScientificImageViewer::ScientificImageViewer(QWidget* parent)
:	QWidget(parent), view(new QGraphicsView(this)), scene(new QGraphicsScene(this)), item(0), source_label(new QLabel(this)), zoom_factor(1.0)
{
	QVBoxLayout* root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);QHBoxLayout*bar=new QHBoxLayout();
	QPushButton*fit=new QPushButton(QStringLiteral("Fit to view"),this);QPushButton*in=new QPushButton(QStringLiteral("Zoom in"),this);QPushButton*out=new QPushButton(QStringLiteral("Zoom out"),this);QPushButton*reset=new QPushButton(QStringLiteral("Reset"),this);QPushButton*open=new QPushButton(QStringLiteral("Open full size"),this);QPushButton*save=new QPushButton(QStringLiteral("Save image"),this);
	bar->addWidget(fit);bar->addWidget(in);bar->addWidget(out);bar->addWidget(reset);bar->addWidget(open);bar->addWidget(save);bar->addStretch(1);root->addLayout(bar);
	view->setScene(scene);view->setDragMode(QGraphicsView::ScrollHandDrag);view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);view->setMinimumHeight(320);view->viewport()->installEventFilter(this);root->addWidget(view);source_label->setWordWrap(true);root->addWidget(source_label);
	connect(fit,&QPushButton::clicked,this,[this](){fitToView();});connect(in,&QPushButton::clicked,this,[this](){setZoom(zoom_factor*1.25);});connect(out,&QPushButton::clicked,this,[this](){setZoom(zoom_factor/1.25);});connect(reset,&QPushButton::clicked,this,[this](){setZoom(1.0);view->centerOn(item);});connect(open,&QPushButton::clicked,this,[this](){if(!image_path.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(image_path));});connect(save,&QPushButton::clicked,this,[this](){if(image_path.isEmpty())return;const QString target=QFileDialog::getSaveFileName(this,QStringLiteral("Save scientific image"),QFileInfo(image_path).fileName(),QStringLiteral("PNG image (*.png);;All files (*.*)"));if(!target.isEmpty()){QFile::remove(target);QFile::copy(image_path,target);}});
	clearImage();
}


void ScientificImageViewer::setImage(const QString& path,const QString& url,const QString& license)
{
	QPixmap pix(path);scene->clear();item=0;image_path=path;if(pix.isNull()){clearImage(QStringLiteral("Not available: image could not be decoded"));return;}item=scene->addPixmap(pix);scene->setSceneRect(item->boundingRect());source_label->setText(QStringLiteral("Loaded | Source: %1 | License: %2").arg(url.isEmpty()?QStringLiteral("Not available"):url,license.isEmpty()?QStringLiteral("Not available"):license));fitToView();
}


void ScientificImageViewer::clearImage(const QString&status){scene->clear();item=0;image_path.clear();source_label->setText(status);}
void ScientificImageViewer::setZoom(double z){if(!item)return;zoom_factor=std::max(0.05,std::min(20.0,z));view->resetTransform();view->scale(zoom_factor,zoom_factor);}
void ScientificImageViewer::fitToView(){if(!item)return;view->fitInView(item,Qt::KeepAspectRatio);zoom_factor=view->transform().m11();}
bool ScientificImageViewer::eventFilter(QObject*w,QEvent*e){if(w==view->viewport()&&e->type()==QEvent::Wheel){QWheelEvent*we=static_cast<QWheelEvent*>(e);setZoom(zoom_factor*(we->angleDelta().y()>0?1.15:1.0/1.15));return true;}return QWidget::eventFilter(w,e);}
