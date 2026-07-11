/*=====================================================================
ScientificImageViewer.h
=====================================================================*/
#pragma once


#include <QtCore/QString>
#include <QtWidgets/QWidget>


class ScientificImageViewer : public QWidget
{
public:
	explicit ScientificImageViewer(QWidget* parent = 0);
	void setImage(const QString& local_path, const QString& source_url, const QString& license_text);
	void clearImage(const QString& status = QStringLiteral("Not available"));

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void setZoom(double zoom);
	void fitToView();
	class QGraphicsView* view;
	class QGraphicsScene* scene;
	class QGraphicsPixmapItem* item;
	class QLabel* source_label;
	QString image_path;
	double zoom_factor;
};
