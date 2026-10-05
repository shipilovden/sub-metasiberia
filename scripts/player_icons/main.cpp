// Offline asset conversion only; the common/SDL player does not depend on Qt.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <cstdio>

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if(argc != 2) return 2; // Input: repository resources/icons/lucide directory.
	const QDir source(QString::fromLocal8Bit(argv[1]));
	if(!source.exists() || !source.mkpath("player")) return 3;
	for(const char* name : { "play", "pause", "volume-2", "volume-x", "minimize-2" })
	{
		QFile svg(source.filePath(QString::fromLatin1(name) + ".svg"));
		if(!svg.open(QIODevice::ReadOnly)) return 4;
		QByteArray bytes = svg.readAll();
		bytes.replace("currentColor", "#ffffff");
		QSvgRenderer renderer(bytes);
		if(!renderer.isValid()) return 5;
		QImage image(120, 120, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		// 24px glyph in a 40px button, stored at 3x for high-DPI displays.
		renderer.render(&painter, QRectF(24, 24, 72, 72));
		painter.end();
		if(!image.save(source.filePath("player/" + QString::fromLatin1(name) + ".png"))) return 6;
		std::printf("Rendered %s\n", name);
	}
	return 0;
}
