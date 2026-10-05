#pragma once

// Qt-only capture geometry, shared with the isolated frame geometry test.
#include <QtCore/QRect>
#include <QtCore/QVariantMap>
#include <cmath>
#include <stdexcept>

namespace PhotoFrameRenderer
{
struct Layout
{
	QSize scene_size;
	QRect crop;
	QSize output_size;
};

inline QRect cropRect(const QSize& size, double aspect)
{
	if(!std::isfinite(aspect) || aspect < 0)
		throw std::runtime_error("Invalid photo aspect ratio");
	int w = size.width(), h = size.height();
	if(aspect > 0)
	{
		if(w > h * aspect) w = qMax(1, qRound(h * aspect));
		else h = qMax(1, qRound(w / aspect));
	}
	return QRect((size.width() - w) / 2, (size.height() - h) / 2, w, h);
}

inline void checkSize(const QSize& size, int hardware_width = 8192, int hardware_height = 8192)
{
	if(size.isEmpty() || size.width() > qMin(8192, hardware_width) || size.height() > qMin(8192, hardware_height) ||
		qint64(size.width()) * size.height() > 36000000)
		throw std::runtime_error("Photo render exceeds the GPU/8192-pixel/36-megapixel limit; select a smaller resolution or crop");
}

inline Layout layout(const QSize& viewport, const QVariantMap& values, const QSize& requested = QSize())
{
	checkSize(viewport);
	const double aspect = values.value(QStringLiteral("aspect_ratio"), 0.0).toDouble();
	const QRect view_crop = cropRect(viewport, aspect);
	QSize bounds = requested;
	if(bounds == QSize())
	{
		const QString resolution = values.value(QStringLiteral("photo_resolution"), QStringLiteral("viewport")).toString();
		if(resolution == QStringLiteral("viewport"))
			return {viewport, view_crop, view_crop.size()};
		const QStringList parts = resolution.toLower().split('x');
		bool ok_w = false, ok_h = false;
		if(parts.size() == 2) bounds = QSize(parts[0].toInt(&ok_w), parts[1].toInt(&ok_h));
		if(!ok_w || !ok_h) throw std::runtime_error("Invalid photo resolution");
	}
	checkSize(bounds);
	const QSize output = view_crop.size().scaled(bounds, Qt::KeepAspectRatio);
	const double scale = qMax(double(output.width()) / view_crop.width(), double(output.height()) / view_crop.height());
	// Scale the entire original view, not the projection/FOV.  The extra pixels
	// outside the composition frame are rendered before applying the centre crop.
	const double w = std::ceil(viewport.width() * scale), h = std::ceil(viewport.height() * scale);
	if(w > 8192 || h > 8192) throw std::runtime_error("Photo source view exceeds 8192 pixels; select a smaller resolution or crop");
	const QSize scene_size{int(w), int(h)};
	checkSize(scene_size);
	const QRect crop = cropRect(scene_size, aspect);
	return {scene_size, crop, output.boundedTo(crop.size())};
}
}
