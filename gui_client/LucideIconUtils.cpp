/*=====================================================================
LucideIconUtils.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "LucideIconUtils.h"


#include <QtCore/QFileInfo>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QAction>
#include <algorithm>


namespace LucideIconUtils
{

QString directoryForBasePath(const std::string& base_dir_path)
{
	const QString packaged = QString::fromUtf8((base_dir_path + "/data/resources/icons/lucide").c_str());
	if(QFileInfo(packaged).isDir())
		return packaged;

	const QString source_tree = QString::fromUtf8((base_dir_path + "/resources/icons/lucide").c_str());
	if(QFileInfo(source_tree).isDir())
		return source_tree;

	return QString();
}


QIcon tintedIcon(const QString& icon_directory, const QString& icon_name, const QColor& colour, const int logical_size)
{
	if(icon_directory.isEmpty() || icon_name.isEmpty() || !colour.isValid())
		return QIcon();

	const QString path = icon_directory + QLatin1Char('/') + icon_name + QStringLiteral(".svg");
	if(!QFileInfo::exists(path))
		return QIcon();

	// Render at 2x and attach the corresponding device-pixel ratio.  This keeps
	// the thin Lucide strokes crisp on ordinary and HiDPI Qt 5 displays.
	const int use_size = std::max(12, logical_size);
	const int pixel_size = use_size * 2;
	QPixmap source = QIcon(path).pixmap(QSize(pixel_size, pixel_size));
	if(source.isNull())
		return QIcon();

	QPixmap tinted(source.size());
	tinted.fill(Qt::transparent);
	{
		QPainter painter(&tinted);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.drawPixmap(0, 0, source);
		painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
		painter.fillRect(tinted.rect(), colour);
	}
	tinted.setDevicePixelRatio(2.0);
	return QIcon(tinted);
}


bool setActionIcon(QAction* action, const QString& icon_directory, const QString& icon_name,
	const QColor& colour, const int logical_size)
{
	if(!action)
		return false;
	const QIcon icon = tintedIcon(icon_directory, icon_name, colour, logical_size);
	if(icon.isNull())
		return false;
	action->setIcon(icon);
	action->setIconVisibleInMenu(true);
	return true;
}


bool setButtonIcon(QAbstractButton* button, const QString& icon_directory, const QString& icon_name,
	const QColor& colour, const int logical_size)
{
	if(!button)
		return false;
	const QIcon icon = tintedIcon(icon_directory, icon_name, colour, logical_size);
	if(icon.isNull())
		return false;
	button->setIcon(icon);
	button->setIconSize(QSize(logical_size, logical_size));
	return true;
}

} // namespace LucideIconUtils
