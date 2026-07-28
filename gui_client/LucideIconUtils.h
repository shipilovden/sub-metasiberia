/*=====================================================================
LucideIconUtils.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <QtGui/QColor>
#include <QtGui/QIcon>
#include <QtGui/QPalette>
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
	QIcon paddedTintedIcon(const QString& icon_directory, const QString& icon_name,
		const QColor& colour, int outer_logical_size, int glyph_logical_size);

	// Keeps semantic accent colours readable on both dark and light themes.
	// A genuinely monochrome palette deliberately falls back to its foreground
	// colour so black/white and high-contrast themes stay monochrome.
	bool isMonochromePalette(const QPalette& palette);
	QColor themeAwareColour(const QColor& semantic_colour, const QPalette& palette,
		QPalette::ColorRole foreground_role, QPalette::ColorRole background_role);

	bool setActionIcon(QAction* action, const QString& icon_directory,
		const QString& icon_name, const QColor& colour, int logical_size = 20);
	bool setPaddedActionIcon(QAction* action, const QString& icon_directory,
		const QString& icon_name, const QColor& colour, int outer_logical_size, int glyph_logical_size);
	bool setButtonIcon(QAbstractButton* button, const QString& icon_directory,
		const QString& icon_name, const QColor& colour, int logical_size = 18);
}
