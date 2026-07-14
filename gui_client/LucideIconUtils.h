/*=====================================================================
LucideIconUtils.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <QtGui/QColor>
#include <QtGui/QIcon>
#include <QtCore/QString>
#include <string>


class QAction;
class QAbstractButton;


namespace LucideIconUtils
{
	// Returns the packaged icon directory, with a source-tree fallback for
	// developer builds that have not copied runtime resources yet.
	QString directoryForBasePath(const std::string& base_dir_path);

	// Lucide SVGs use currentColor.  Qt 5 does not consistently resolve that
	// value for QIcon, so the rendered alpha mask is tinted explicitly.
	QIcon tintedIcon(const QString& icon_directory, const QString& icon_name,
		const QColor& colour, int logical_size = 20);

	bool setActionIcon(QAction* action, const QString& icon_directory,
		const QString& icon_name, const QColor& colour, int logical_size = 20);
	bool setButtonIcon(QAbstractButton* button, const QString& icon_directory,
		const QString& icon_name, const QColor& colour, int logical_size = 18);
}
