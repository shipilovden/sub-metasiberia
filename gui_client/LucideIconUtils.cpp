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
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtGui/QAction>
#else
#include <QtWidgets/QAction>
#endif
#include <algorithm>
#include <cmath>


namespace
{

static qreal linearColourChannel(const qreal value)
{
	return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}


static qreal relativeLuminance(const QColor& colour)
{
	return
		0.2126 * linearColourChannel(colour.redF()) +
		0.7152 * linearColourChannel(colour.greenF()) +
		0.0722 * linearColourChannel(colour.blueF());
}


static qreal contrastRatio(const QColor& first, const QColor& second)
{
	const qreal first_luminance = relativeLuminance(first);
	const qreal second_luminance = relativeLuminance(second);
	const qreal lighter = std::max(first_luminance, second_luminance);
	const qreal darker = std::min(first_luminance, second_luminance);
	return (lighter + 0.05) / (darker + 0.05);
}

} // anonymous namespace


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


QIcon paddedTintedIcon(const QString& icon_directory, const QString& icon_name, const QColor& colour,
	const int outer_logical_size, const int glyph_logical_size)
{
	if(icon_directory.isEmpty() || icon_name.isEmpty() || !colour.isValid())
		return QIcon();

	const QString path = icon_directory + QLatin1Char('/') + icon_name + QStringLiteral(".svg");
	if(!QFileInfo::exists(path))
		return QIcon();

	const int outer_size = std::max(12, outer_logical_size);
	const int glyph_size = std::max(8, std::min(glyph_logical_size, outer_size));
	const int outer_pixel_size = outer_size * 2;
	const int glyph_pixel_size = glyph_size * 2;
	QPixmap source = QIcon(path).pixmap(QSize(glyph_pixel_size, glyph_pixel_size));
	if(source.isNull())
		return QIcon();

	QPixmap padded(outer_pixel_size, outer_pixel_size);
	padded.fill(Qt::transparent);
	{
		QPainter painter(&padded);
		painter.setRenderHint(QPainter::Antialiasing, true);
		const int offset = (outer_pixel_size - glyph_pixel_size) / 2;
		painter.drawPixmap(offset, offset, glyph_pixel_size, glyph_pixel_size, source);
		painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
		painter.fillRect(padded.rect(), colour);
	}
	padded.setDevicePixelRatio(2.0);
	return QIcon(padded);
}


bool isMonochromePalette(const QPalette& palette)
{
	// Window surfaces are normally neutral even in colourful themes, so use the
	// interactive roles to identify a deliberately black/white palette.
	return
		palette.color(QPalette::Highlight).hslSaturationF() < 0.08 &&
		palette.color(QPalette::Link).hslSaturationF() < 0.08 &&
		palette.color(QPalette::LinkVisited).hslSaturationF() < 0.08;
}


QColor themeAwareColour(const QColor& semantic_colour, const QPalette& palette,
	const QPalette::ColorRole foreground_role, const QPalette::ColorRole background_role)
{
	const QColor foreground = palette.color(foreground_role);
	if(!semantic_colour.isValid() || isMonochromePalette(palette))
		return foreground;

	const QColor background = palette.color(background_role);
	QColor adjusted = semantic_colour.toHsl();
	const qreal minimum_contrast = 4.0; // Thin Lucide strokes need stronger contrast than filled controls.
	if(contrastRatio(adjusted, background) >= minimum_contrast)
		return adjusted;

	const bool use_lighter_colour = relativeLuminance(foreground) > relativeLuminance(background);
	for(int i=0; i<24; ++i)
	{
		const qreal lightness = adjusted.lightnessF();
		adjusted.setHslF(
			adjusted.hslHueF(),
			adjusted.hslSaturationF(),
			use_lighter_colour ? std::min(1.0, lightness + 0.035) : std::max(0.0, lightness - 0.035),
			adjusted.alphaF()
		);
		if(contrastRatio(adjusted, background) >= minimum_contrast)
			return adjusted;
	}

	return foreground;
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


bool setPaddedActionIcon(QAction* action, const QString& icon_directory, const QString& icon_name,
	const QColor& colour, const int outer_logical_size, const int glyph_logical_size)
{
	if(!action)
		return false;
	const QIcon icon = paddedTintedIcon(icon_directory, icon_name, colour, outer_logical_size, glyph_logical_size);
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
