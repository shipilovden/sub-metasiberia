#pragma once
// Qt boundary: the same composition geometry and artistic toning are used for
// the GL preview, photo export and video frames.
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtCore/QRect>
#include <QtCore/QVariantMap>
#include <cmath>

namespace PhotoModeEffects {
inline QRect frameRect(const QSize& size, double aspect) {
	if(aspect <= 0 || size.isEmpty()) return QRect(QPoint(), size);
	int w = size.width(), h = size.height();
	if(w > h * aspect) w = qMax(1, qRound(h * aspect));
	else h = qMax(1, qRound(w / aspect));
	return QRect((size.width()-w)/2, (size.height()-h)/2, w, h);
}
inline QColor tone(double amount, bool green_magenta) {
	return green_magenta ? (amount >= 0 ? QColor(225, 115, 220) : QColor(115, 225, 155)) :
		(amount >= 0 ? QColor(255, 175, 85) : QColor(95, 160, 255));
}
inline double vignette(double x, double y) {
	const double edge = qBound(0.0, (x*x + y*y - 0.25) / 1.75, 1.0);
	return edge * edge * 0.85;
}
inline void apply(QImage& image, const QVariantMap& values) {
	const double warm = values.value("warmth", 0.0).toDouble();
	const double tint = values.value("tint", 0.0).toDouble();
	const double strength = values.value("vignette", 0.0).toDouble();
	if(warm == 0 && tint == 0 && strength == 0) return;
	image = image.convertToFormat(QImage::Format_RGB888);
	const QColor a = tone(warm, false), b = tone(tint, true);
	const double alpha_a = std::abs(warm)*0.2, alpha_b = std::abs(tint)*0.2;
	for(int y=0; y<image.height(); ++y) {
		uchar* row = image.scanLine(y);
		const double ny = 2.0*(y+0.5)/image.height()-1;
		for(int x=0; x<image.width(); ++x) {
			const double shade = 1-strength*vignette(2.0*(x+0.5)/image.width()-1, ny);
			for(int c=0; c<3; ++c) {
				const int ca = c==0 ? a.red() : c==1 ? a.green() : a.blue();
				const int cb = c==0 ? b.red() : c==1 ? b.green() : b.blue();
				row[x*3+c] = (uchar)qBound(0, qRound(((row[x*3+c]*(1-alpha_a)+ca*alpha_a)*(1-alpha_b)+cb*alpha_b)*shade), 255);
			}
		}
	}
}
}
