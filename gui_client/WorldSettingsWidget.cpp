/*=====================================================================
WorldSettingsWidget.cpp
-----------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "WorldSettingsWidget.h"


#include "MainWindow.h"
#include "HTTPClient.h"
#include "TerrainSystem.h"
#include "../shared/WorldMaterial.h"
#include "MCPTextureTools.h"
#include "MapWorldUtils.h"
#include "../qt/SignalBlocker.h"
#include "../shared/ResourceManager.h"
#include <graphics/EXRDecoder.h>
#include <qt/QtUtils.h>
#include <Exception.h>
#include <FileUtils.h>
#include <FileChecksum.h>
#include <PlatformUtils.h>
#include <StringUtils.h>
#include <QtCore/QMutexLocker>
#include <QtCore/QUuid>
#include <QtCore/QSettings>
#include <QtCore/QEvent>
#include <QtCore/QCoreApplication>
#include <QtCore/QRunnable>
#include <QtCore/QPointer>
#include <QtCore/QThreadPool>
#include <QtCore/QObject>
#include <QtCore/QMetaObject>
#include <QtCore/QBuffer>
#include <QtCore/QUrl>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtGui/QImage>
#include <QtGui/QIcon>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>
#include <QtGui/QPolygonF>
#include <QtGui/QWheelEvent>
#include <QtGui/QPainter>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QDockWidget>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QDir>
#include <QtCore/QThread>
#include <QtCore/QPointer>
#include <QtCore/QMetaObject>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLineEdit>
#include <QtCore/QRandomGenerator>
#include <QtGui/QStandardItemModel>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>


static QIcon makeTerrainLayerPreviewIcon(const QColor& colour)
{
	QPixmap pixmap(44, 30);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(QColor(12, 14, 17), 1));
	painter.setBrush(QColor(24, 27, 31));
	painter.drawRoundedRect(QRectF(0.5, 0.5, 43, 29), 4, 4);
	QLinearGradient gradient(4, 5, 4, 25);
	gradient.setColorAt(0.0, colour.lighter(145));
	gradient.setColorAt(1.0, colour.darker(150));
	painter.setPen(Qt::NoPen);
	painter.setBrush(gradient);
	QPainterPath hill;
	hill.moveTo(4, 23);
	hill.cubicTo(9, 20, 10, 12, 17, 13);
	hill.cubicTo(23, 14, 25, 7, 31, 10);
	hill.cubicTo(36, 12, 37, 18, 40, 20);
	hill.lineTo(40, 25);
	hill.lineTo(4, 25);
	hill.closeSubpath();
	painter.drawPath(hill);
	painter.setPen(QPen(colour.lighter(180), 1));
	painter.drawArc(QRectF(10, 10, 22, 11), 195 * 16, 145 * 16);
	painter.drawArc(QRectF(14, 12, 14, 7), 195 * 16, 145 * 16);
	return QIcon(pixmap);
}


static QIcon makeSculptToolPreviewIcon(int tool_id)
{
	QPixmap pixmap(48, 30);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(QColor(18, 20, 24), 1));
	painter.setBrush(QColor(29, 32, 37));
	painter.drawRoundedRect(QRectF(0.5, 0.5, 47, 29), 4, 4);
	painter.setPen(QPen(QColor(66, 72, 79), 1));
	for(int x=8; x<45; x+=9) painter.drawLine(x, 4, x, 26);
	for(int y=8; y<27; y+=8) painter.drawLine(4, y, 44, y);

	const int local_id = tool_id >= 38 ? tool_id - 38 : (tool_id >= 17 ? tool_id - 17 : tool_id);
	const bool lowers = tool_id == 2 || tool_id == 5 || tool_id == 6 || tool_id == 7 || tool_id == 9 ||
		tool_id == 11 || tool_id == 21 || tool_id == 22 || tool_id == 26 || tool_id == 27 || tool_id == 29 || tool_id == 37;
	const QColor fill_colour = lowers ? QColor(93, 154, 207) : QColor(219, 171, 61);
	const qreal base_y = 24.0;
	QPainterPath profile;
	profile.moveTo(4, base_y);
	for(int i=0; i<=32; ++i)
	{
		const qreal t = (qreal)i / 32.0;
		const qreal x = 4.0 + 40.0 * t;
		const qreal u = (t - 0.5) * 2.0;
		const qreal edge = std::max(0.0, 1.0 - u*u);
		qreal shape = std::sqrt(edge);
		if(local_id % 7 == 0) shape = std::pow(edge, 0.35);                 // broad ridge
		else if(local_id % 7 == 1) shape = std::pow(edge, 1.7);             // cone / pointed peak
		else if(local_id % 7 == 2) shape = std::min(1.0, edge * 3.2);       // plateau
		else if(local_id % 7 == 3) shape = edge * (0.7 + 0.3 * std::cos(u * 7.0)); // uneven hills
		else if(local_id % 7 == 4) shape = 1.0 - std::pow(edge, 0.7);       // crater / basin
		else if(local_id % 7 == 5) shape = std::pow(edge, 0.55) * (0.72 + 0.28 * std::cos(u * 3.5)); // saddle / dunes
		else shape = std::pow(edge, 0.65) * (0.5 + 0.5 * std::sin((t * 2.0 + 0.1) * 3.141592653589793));
		const qreal amplitude = 7.0 * shape;
		const qreal y = lowers ? base_y - (7.0 - amplitude) : base_y - amplitude;
		if(i == 0) profile.lineTo(x, y); else profile.lineTo(x, y);
	}
	profile.lineTo(44, base_y);
	profile.closeSubpath();
	painter.setPen(Qt::NoPen);
	painter.setBrush(fill_colour);
	painter.drawPath(profile);
	painter.setPen(QPen(fill_colour.lighter(165), 1.2));
	painter.drawPath(profile);
	return QIcon(pixmap);
}


namespace
{
const int REAL_TERRAIN_MAP_RESOLUTION = 512;
const double REAL_TERRAIN_MAX_MERCATOR_LATITUDE = 85.05112878;
}

class RealTerrainMapWidget : public QWidget
{
public:
	explicit RealTerrainMapWidget(QWidget* parent = NULL) : QWidget(parent), center_x(0), center_y(0), zoom(8), basemap(0),
		selection_active(false), dragging(false), freehand(false), panning(false)
	{
		setMinimumHeight(260);
		setMinimumWidth(260);
		setMouseTracking(true);
		center_x = (55.75 + 180.0) / 360.0 * 256.0 * (1 << zoom);
		const double lat = 55.75 * 3.14159265358979323846 / 180.0;
		center_y = (0.5 - std::log((1.0 + std::sin(lat)) / (1.0 - std::sin(lat))) / (4.0 * 3.14159265358979323846)) * 256.0 * (1 << zoom);
	}

	void setFreehand(bool enabled) { freehand = enabled; clearSelection(); }
	void setBasemap(int source) { if(source != basemap) { basemap = source; tiles.clear(); requested.clear(); update(); } }
	void zoomBy(int delta)
	{
		if(delta == 0) return;
		const int new_zoom = myClamp(zoom + delta, 2, 19);
		if(new_zoom == zoom) return;
		zoomAt(new_zoom, QPointF(width() * 0.5, height() * 0.5));
	}
	bool hasSelection() const { return selection_active && selection_points.size() >= 3; }
	void clearSelection() { selection_active = false; selection_points.clear(); update(); }
	QVector<QPointF> getSelectionPolygon() const
	{
		QVector<QPointF> result;
		const QPointF origin = worldOrigin();
		const double world_size = 256.0 * (1 << zoom);
		for(const QPointF& p : selection_points)
		{
			const QPointF w = origin + p;
			const double lon = w.x() / world_size * 360.0 - 180.0;
			const double n = 3.14159265358979323846 - 2.0 * 3.14159265358979323846 * w.y() / world_size;
			const double lat = 180.0 / 3.14159265358979323846 * std::atan(std::sinh(n));
			result << QPointF(lon, lat);
		}
		return result;
	}
	void getSelection(double& north, double& south, double& west, double& east) const
	{
		const QPointF origin = worldOrigin();
		double min_lon = 180.0, max_lon = -180.0, min_lat = 90.0, max_lat = -90.0;
		for(const QPointF& p : selection_points)
		{
			const QPointF w = origin + p;
			const double lon = w.x() / (256.0 * (1 << zoom)) * 360.0 - 180.0;
			const double n = 3.14159265358979323846 - 2.0 * 3.14159265358979323846 * w.y() / (256.0 * (1 << zoom));
			const double lat = 180.0 / 3.14159265358979323846 * std::atan(std::sinh(n));
			min_lon = std::min(min_lon, lon); max_lon = std::max(max_lon, lon);
			min_lat = std::min(min_lat, lat); max_lat = std::max(max_lat, lat);
		}
		north = max_lat; south = min_lat; west = min_lon; east = max_lon;
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), QColor(222, 226, 220));
		const QPointF origin = worldOrigin();
		const int first_x = (int)std::floor(origin.x() / 256.0), first_y = (int)std::floor(origin.y() / 256.0);
		const int last_x = (int)std::floor((origin.x() + width()) / 256.0), last_y = (int)std::floor((origin.y() + height()) / 256.0);
		for(int ty=first_y; ty<=last_y; ++ty)
		for(int tx=first_x; tx<=last_x; ++tx)
		{
			const int wrapped_x = (tx % (1 << zoom) + (1 << zoom)) % (1 << zoom);
			const QString key = QStringLiteral("%1/%2/%3/%4").arg(basemap).arg(zoom).arg(wrapped_x).arg(ty);
			const QRectF target(tx * 256.0 - origin.x(), ty * 256.0 - origin.y(), 256, 256);
			const auto it = tiles.constFind(key);
			if(it != tiles.constEnd()) painter.drawImage(target, it.value());
			else { painter.fillRect(target, QColor(232, 234, 227)); painter.setPen(QColor(200, 203, 197)); painter.drawRect(target); }
			requestTile(tx, ty);
		}
		if(selection_active && !selection_points.isEmpty())
		{
			QPolygonF poly(selection_points);
			if(dragging && !freehand) { poly.clear(); poly << drag_start << cursor_pos << QPointF(drag_start.x(), cursor_pos.y()) << QPointF(cursor_pos.x(), drag_start.y()); }
			painter.setPen(QPen(QColor(255, 205, 60), 2));
			painter.setBrush(QColor(255, 205, 60, 60));
			if(poly.size() >= 3) painter.drawPolygon(poly); else if(poly.size() == 2) painter.drawLine(poly[0], poly[1]);
		}
	}
	void mousePressEvent(QMouseEvent* e) override
	{
		if(e->button() == Qt::RightButton) { panning = true; last_mouse = e->pos(); return; }
		if(e->button() != Qt::LeftButton) return;
		dragging = true; drag_start = e->pos(); cursor_pos = drag_start;
		if(freehand) { selection_points.clear(); selection_points << e->pos(); }
		else { selection_points.clear(); selection_points << drag_start << drag_start << drag_start; }
		selection_active = true; update();
	}
	void mouseMoveEvent(QMouseEvent* e) override
	{
		if(panning) { center_x -= e->pos().x() - last_mouse.x(); center_y -= e->pos().y() - last_mouse.y(); last_mouse=e->pos(); update(); return; }
		if(!dragging) return;
		cursor_pos = e->pos();
		if(freehand && (selection_points.isEmpty() || (selection_points.last() - e->pos()).manhattanLength() >= 3)) selection_points << e->pos();
		else if(!freehand) selection_points[1] = e->pos();
		update();
	}
	void mouseReleaseEvent(QMouseEvent* e) override
	{
		if(e->button() == Qt::RightButton) { panning = false; return; }
		if(e->button() != Qt::LeftButton || !dragging) return;
		dragging = false; cursor_pos=e->pos();
		if(freehand && selection_points.size() > 2) selection_points << selection_points.first();
		else if(!freehand) { selection_points.clear(); selection_points << drag_start << QPointF(cursor_pos.x(), drag_start.y()) << cursor_pos << QPointF(drag_start.x(), cursor_pos.y()); }
		update();
	}
	void wheelEvent(QWheelEvent* e) override
	{
		e->accept(); // Keep the parent settings scroll area from consuming map zoom.
		const int delta = e->angleDelta().y();
		if(delta == 0) return;
		const int new_zoom = myClamp(zoom + (delta > 0 ? 1 : -1), 2, 19);
		if(new_zoom == zoom) return;
		zoomAt(new_zoom, e->position());
	}

private:
	QPointF worldOrigin() const { return QPointF(center_x - width()*0.5, center_y - height()*0.5); }
	void zoomAt(int new_zoom, const QPointF& cursor)
	{
		const QPointF old_origin = worldOrigin();
		const int old_zoom = zoom;
		const QPointF world_at_cursor = old_origin + cursor;
		zoom = new_zoom;
		const double factor = (double)(1 << new_zoom) / (double)(1 << old_zoom);
		center_x = world_at_cursor.x() * factor - cursor.x() + width() * 0.5;
		center_y = world_at_cursor.y() * factor - cursor.y() + height() * 0.5;
		const QPointF new_origin = worldOrigin();
		for(QPointF& point : selection_points)
			point = (old_origin + point) * factor - new_origin;
		drag_start = (old_origin + drag_start) * factor - new_origin;
		cursor_pos = (old_origin + cursor_pos) * factor - new_origin;
		update();
	}
	void requestTile(int x, int y)
	{
		const int limit = 1 << zoom;
		if(y < 0 || y >= limit) return;
		x = (x % limit + limit) % limit;
		const QString key = QStringLiteral("%1/%2/%3/%4").arg(basemap).arg(zoom).arg(x).arg(y);
		if(tiles.contains(key) || requested.contains(key)) return;
		if(requested.size() >= 24) return;
		requested.insert(key);
		const int z = zoom;
		const int source = basemap;
		QPointer<RealTerrainMapWidget> self(this);
		QThreadPool::globalInstance()->start(QRunnable::create([self, key, x, y, z, source]()
		{
			QImage image;
			try
			{
				HTTPClient client; client.user_agent = "Metasiberia terrain map";
				std::vector<uint8> data;
				const std::string url = source == 1 ?
					("https://tile.opentopomap.org/" + toString(z) + "/" + toString(x) + "/" + toString(y) + ".png") :
					source == 2 ?
					("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/" + toString(z) + "/" + toString(y) + "/" + toString(x)) :
					toStdString(MapWorldUtils::makeOSMTileURL("vr.metasiberia.com", x, y, z));
				const HTTPClient::ResponseInfo r = client.downloadFile(url, data);
				if(r.response_code == 200 && !data.empty()) image.loadFromData(data.data(), (uint)data.size());
			}
			catch(...) {}
			if(self) QMetaObject::invokeMethod(self, [self, key, image]() { if(self) { self->requested.remove(key); if(!image.isNull()) self->tiles.insert(key, image); self->update(); } }, Qt::QueuedConnection);
		}));
	}
	QHash<QString, QImage> tiles;
	QSet<QString> requested;
	double center_x, center_y;
	int zoom;
	int basemap;
	bool selection_active, dragging, freehand, panning;
	QPointF drag_start, cursor_pos;
	QPoint last_mouse;
	QVector<QPointF> selection_points;
};

namespace
{
struct RealTerrainTileRange
{
	int min_x, max_x, min_y, max_y;
	int tile_count;
};

static double realTerrainMercatorX(double longitude, int zoom)
{
	return (longitude + 180.0) / 360.0 * (double)(1 << zoom);
}

static double realTerrainMercatorY(double latitude, int zoom)
{
	const double lat_rad = latitude * 3.14159265358979323846 / 180.0;
	return (0.5 - std::log((1.0 + std::sin(lat_rad)) / (1.0 - std::sin(lat_rad))) /
		(4.0 * 3.14159265358979323846)) * (double)(1 << zoom);
}

static RealTerrainTileRange realTerrainTileRange(double north, double south, double west, double east, int zoom)
{
	const int tile_limit = (1 << zoom) - 1;
	const int min_x = myClamp((int)std::floor(realTerrainMercatorX(west, zoom)), 0, tile_limit);
	const int max_x = myClamp((int)std::floor(realTerrainMercatorX(east, zoom)), 0, tile_limit);
	const int min_y = myClamp((int)std::floor(realTerrainMercatorY(north, zoom)), 0, tile_limit);
	const int max_y = myClamp((int)std::floor(realTerrainMercatorY(south, zoom)), 0, tile_limit);
	return { min_x, max_x, min_y, max_y, (max_x - min_x + 1) * (max_y - min_y + 1) };
}

struct RealTerrainOSMFeature
{
	QVector<QPointF> points; // longitude, latitude
	QString kind;
};

static QByteArray encodeTerrainPng(const QImage& image)
{
	QByteArray data;
	QBuffer buffer(&data);
	if(!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
		throw glare::Exception("could not encode a terrain mask as PNG");
	return data;
}

static QImage rasterizeTerrainSelection(int resolution, double north, double south, double west, double east, const QVector<QPointF>& selection)
{
	QImage rgba(resolution, resolution, QImage::Format_RGB32); rgba.fill(Qt::black);
	QPolygonF polygon;
	for(const QPointF& geo : selection)
		polygon << QPointF((geo.x()-west)/(east-west)*(resolution-1), (north-geo.y())/(north-south)*(resolution-1));
	QPainter painter(&rgba); painter.setRenderHint(QPainter::Antialiasing, false); painter.setPen(Qt::NoPen); painter.setBrush(Qt::white); painter.drawPolygon(polygon); painter.end();
	return rgba.convertToFormat(QImage::Format_Grayscale8);
}

static std::vector<float> terrainSelectionEdgeDistance(const QImage& selection_mask)
{
	const int width=selection_mask.width(), height=selection_mask.height();
	const float infinity=1.0e6f, diagonal=1.41421356f;
	std::vector<float> distance((size_t)width*height, infinity);
	for(int y=0;y<height;++y)
	{
		const uchar* row=selection_mask.constScanLine(y);
		for(int x=0;x<width;++x)
		{
			if(row[x]<128) distance[(size_t)y*width+x]=0.f;
			else if(x==0 || y==0 || x==width-1 || y==height-1) distance[(size_t)y*width+x]=0.5f;
		}
	}
	for(int y=0;y<height;++y) for(int x=0;x<width;++x)
	{
		float& d=distance[(size_t)y*width+x];
		if(x>0)d=std::min(d,distance[(size_t)y*width+x-1]+1.f);
		if(y>0)d=std::min(d,distance[(size_t)(y-1)*width+x]+1.f);
		if(x>0&&y>0)d=std::min(d,distance[(size_t)(y-1)*width+x-1]+diagonal);
		if(x+1<width&&y>0)d=std::min(d,distance[(size_t)(y-1)*width+x+1]+diagonal);
	}
	for(int y=height-1;y>=0;--y) for(int x=width-1;x>=0;--x)
	{
		float& d=distance[(size_t)y*width+x];
		if(x+1<width)d=std::min(d,distance[(size_t)y*width+x+1]+1.f);
		if(y+1<height)d=std::min(d,distance[(size_t)(y+1)*width+x]+1.f);
		if(x+1<width&&y+1<height)d=std::min(d,distance[(size_t)(y+1)*width+x+1]+diagonal);
		if(x>0&&y+1<height)d=std::min(d,distance[(size_t)(y+1)*width+x-1]+diagonal);
	}
	return distance;
}

static QString queryTerrainOSMFeatures(HTTPClient& client, double north, double south, double west, double east,
	const QVector<QPointF>& selection, const QByteArray& height_data, float water_z,
	QByteArray& material_png, QByteArray& tree_png, QByteArray& roads_png, QByteArray& buildings_png)
{
	const int n = REAL_TERRAIN_MAP_RESOLUTION;
	const QImage selection_mask = rasterizeTerrainSelection(n, north, south, west, east, selection);
	QImage roads_rgba(n, n, QImage::Format_RGBA8888); roads_rgba.fill(Qt::transparent);
	QImage buildings_rgba(n, n, QImage::Format_RGBA8888); buildings_rgba.fill(Qt::transparent);
	QVector<RealTerrainOSMFeature> roads, buildings;
	QString feature_status = QCoreApplication::translate("WorldSettingsWidget", "OSM roads and buildings: unavailable; their masks are empty.");
	const double selected_width = (east-west) * 111320.0 * std::cos((north+south)*0.5*3.14159265358979323846/180.0);
	const double selected_height = (north-south) * 111320.0;
	if((east-west) <= 0.25 && (north-south) <= 0.25 && selected_width > 0 && selected_height > 0)
	{
		const QString query = QStringLiteral("[out:json][timeout:20];(way[\"highway\"](%1,%2,%3,%4);way[\"building\"](%1,%2,%3,%4););out geom;")
			.arg(south, 0, 'f', 7).arg(west, 0, 'f', 7).arg(north, 0, 'f', 7).arg(east, 0, 'f', 7);
		const QByteArray post_body = "data=" + QUrl::toPercentEncoding(query);
		const char* endpoints[] = { "https://overpass-api.de/api/interpreter", "https://overpass.kumi.systems/api/interpreter" };
		QString last_error;
		for(const char* endpoint : endpoints)
		{
			try
			{
				std::string response;
				const HTTPClient::ResponseInfo result = client.sendPost(endpoint, post_body.toStdString(), "application/x-www-form-urlencoded", response);
				if(result.response_code != 200) throw glare::Exception("Overpass returned HTTP " + toString(result.response_code));
				QJsonParseError parse_error;
				const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(response), &parse_error);
				if(parse_error.error != QJsonParseError::NoError || !document.isObject()) throw glare::Exception("Overpass returned invalid JSON");
				const QJsonArray elements = document.object().value(QStringLiteral("elements")).toArray();
				static const QStringList ignored = { QStringLiteral("footway"), QStringLiteral("path"), QStringLiteral("cycleway"), QStringLiteral("pedestrian"), QStringLiteral("steps"), QStringLiteral("bridleway"), QStringLiteral("corridor"), QStringLiteral("construction"), QStringLiteral("proposed") };
				for(const QJsonValue& element : elements)
				{
					const QJsonObject object = element.toObject();
					if(object.value(QStringLiteral("type")).toString() != QStringLiteral("way")) continue;
					const QJsonObject tags = object.value(QStringLiteral("tags")).toObject();
					const QString road_kind = tags.value(QStringLiteral("highway")).toString();
					const bool is_road = !road_kind.isEmpty() && !ignored.contains(road_kind);
					const bool is_building = !tags.value(QStringLiteral("building")).toString().isEmpty() || tags.value(QStringLiteral("building")).toBool();
					if(!is_road && !is_building) continue;
					QVector<QPointF> points;
					for(const QJsonValue& point_value : object.value(QStringLiteral("geometry")).toArray())
					{
						const QJsonObject point = point_value.toObject();
						points << QPointF(point.value(QStringLiteral("lon")).toDouble(), point.value(QStringLiteral("lat")).toDouble());
					}
					if(is_road && points.size() >= 2) roads << RealTerrainOSMFeature{points, road_kind};
					if(is_building && points.size() >= 3) buildings << RealTerrainOSMFeature{points, QString()};
				}
				feature_status = QCoreApplication::translate("WorldSettingsWidget", "OpenStreetMap features: %1 roads, %2 buildings.").arg(roads.size()).arg(buildings.size());
				last_error.clear();
				break;
			}
			catch(const std::exception& e) { last_error = QString::fromUtf8(e.what()); }
		}
		if(!last_error.isEmpty()) feature_status = QCoreApplication::translate("WorldSettingsWidget", "OSM unavailable (%1); road/building masks are empty.").arg(last_error);
	}
	else feature_status = QCoreApplication::translate("WorldSettingsWidget", "Selected area is too large for OSM detail; road/building masks are empty.");

	auto map_point = [=](const QPointF& geo) { return QPointF((geo.x()-west)/(east-west)*(n-1), (north-geo.y())/(north-south)*(n-1)); };
	{
		QPainter painter(&roads_rgba); painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		for(const RealTerrainOSMFeature& road : roads)
		{
			double width_m = 5.0;
			if(road.kind == "motorway") width_m=12; else if(road.kind == "trunk") width_m=11; else if(road.kind == "primary") width_m=9;
			else if(road.kind == "secondary") width_m=8; else if(road.kind == "tertiary") width_m=7; else if(road.kind == "residential" || road.kind == "unclassified") width_m=6;
			const double cell_m = std::min(selected_width, selected_height)/(n-1);
			painter.setPen(QPen(Qt::white, std::max(1.0, width_m/std::max(0.01, cell_m)), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			for(int i=1; i<road.points.size(); ++i) painter.drawLine(map_point(road.points[i-1]), map_point(road.points[i]));
		}
	}
	{
		QPainter painter(&buildings_rgba); painter.setRenderHint(QPainter::Antialiasing, true); painter.setPen(Qt::NoPen); painter.setBrush(Qt::white);
		for(const RealTerrainOSMFeature& building : buildings)
		{
			QPolygonF polygon;
			for(const QPointF& point : building.points) polygon << map_point(point);
			if(polygon.size() >= 3) painter.drawPolygon(polygon);
		}
	}
	QImage road_gray = roads_rgba.convertToFormat(QImage::Format_Grayscale8);
	QImage building_gray = buildings_rgba.convertToFormat(QImage::Format_Grayscale8);
	const float* heights = reinterpret_cast<const float*>(height_data.constData());
	QImage material(n, n, QImage::Format_RGBA8888), tree(n, n, QImage::Format_Grayscale8);
	const double cell_m = std::min(selected_width, selected_height)/(n-1);
	for(int y=0; y<n; ++y)
	{
		uchar* material_row = material.scanLine(y);
		uchar* tree_row = tree.scanLine(y);
		uchar* road_row = road_gray.scanLine(y);
		uchar* building_row = building_gray.scanLine(y);
		const uchar* selection_row = selection_mask.constScanLine(y);
		for(int x=0; x<n; ++x)
		{
			const int i=y*n+x, left=y*n+std::max(0,x-1), right=y*n+std::min(n-1,x+1), up=std::max(0,y-1)*n+x, down=std::min(n-1,y+1)*n+x;
			const bool inside=selection_row[x]>=128;
			if(!inside) { road_row[x]=0; building_row[x]=0; }
			const float h=heights[i];
			const float sx=std::fabs(heights[right]-heights[left])/(float)(std::max(1, std::min(n-1, x+1)-std::max(0,x-1))*cell_m);
			const float sy=std::fabs(heights[down]-heights[up])/(float)(std::max(1, std::min(n-1, y+1)-std::max(0,y-1))*cell_m);
			uchar r=0,g=255,b=0;
			if(inside && h > water_z+4.0f)
			{
				if(std::sqrt(sx*sx+sy*sy)>0.25f) { r=255; g=0; b=0; }
				else { r=0; g=0; b=255; }
			}
			float beach_strength = 0.f;
			const float height_above_water = h - water_z;
			const float slope = std::sqrt(sx*sx + sy*sy);
			if(inside && height_above_water > 0.15f && height_above_water < 4.0f && slope < 0.22f)
			{
				float inland = myClamp((height_above_water - 0.15f) / 3.85f, 0.f, 1.f);
				inland = inland * inland * (3.f - 2.f * inland);
				const float flatness = myClamp((0.22f - slope) / 0.14f, 0.f, 1.f);
				beach_strength = (1.f - inland) * flatness;
			}
			const int o=x*4; material_row[o]=r; material_row[o+1]=g; material_row[o+2]=b;
			material_row[o+3]=(uchar)myClamp((int)std::round(255.f * (1.f - beach_strength)), 0, 255);
			tree_row[x]=(inside && h>water_z+1.5f && b>=128 && road_row[x]<16 && building_row[x]<16) ? 255 : 0;
		}
	}
	material_png=encodeTerrainPng(material);
	tree_png=encodeTerrainPng(tree);
	roads_png=encodeTerrainPng(road_gray);
	buildings_png=encodeTerrainPng(building_gray);
	return feature_status;
}
}


RealTerrainImportThread::RealTerrainImportThread(double north_, double south_, double west_, double east_, const QVector<QPointF>& polygon_, float water_z_, QObject* parent)
: QThread(parent),
	north(north_),
	south(south_),
	west(west_),
	east(east_),
	polygon(polygon_),
	water_z(water_z_),
	active_client(NULL)
{}


RealTerrainImportThread::~RealTerrainImportThread()
{
	cancelImport();
	wait();
}


void RealTerrainImportThread::cancelImport()
{
	requestInterruption();
	QMutexLocker lock(&client_mutex);
	if(active_client)
		active_client->kill();
}


void RealTerrainImportThread::run()
{
	HTTPClient client;
	client.user_agent = "Metasiberia terrain importer";
	client.max_data_size = 64u * 1024u * 1024u;
	{
		QMutexLocker lock(&client_mutex);
		active_client = &client;
	}

	try
	{
		if(!std::isfinite(north) || !std::isfinite(south) || !std::isfinite(west) || !std::isfinite(east) || !std::isfinite(water_z) || polygon.size() < 3 ||
			north > REAL_TERRAIN_MAX_MERCATOR_LATITUDE || south < -REAL_TERRAIN_MAX_MERCATOR_LATITUDE ||
			north <= south || west < -180.0 || east > 180.0 || east <= west || (east - west) > 30.0 || (north - south) > 30.0)
			throw glare::Exception("invalid map selection; choose an area within the visible map bounds");
		double import_north = north, import_south = south, import_west = west, import_east = east;
		const double centre_lat_rad = (north + south) * 0.5 * 3.14159265358979323846 / 180.0;
		const double selected_width_m = (east - west) * 111320.0 * std::cos(centre_lat_rad);
		const double selected_height_m = (north - south) * 111320.0;
		if(selected_width_m > selected_height_m)
		{
			const double half_lat_span = selected_width_m / 111320.0 * 0.5;
			import_north = (north + south) * 0.5 + half_lat_span;
			import_south = (north + south) * 0.5 - half_lat_span;
		}
		else
		{
			const double half_lon_span = selected_height_m / (111320.0 * std::max(0.01, std::cos(centre_lat_rad))) * 0.5;
			import_west = (west + east) * 0.5 - half_lon_span;
			import_east = (west + east) * 0.5 + half_lon_span;
		}

		int zoom = 14;
		RealTerrainTileRange range = realTerrainTileRange(import_north, import_south, import_west, import_east, zoom);
		while(range.tile_count > 64 && zoom > 8)
		{
			--zoom;
			range = realTerrainTileRange(import_north, import_south, import_west, import_east, zoom);
		}
		if(range.tile_count < 1 || range.tile_count > 64)
			throw glare::Exception("the selected area needs too many elevation tiles");
		const QImage selection_mask = rasterizeTerrainSelection(REAL_TERRAIN_MAP_RESOLUTION, import_north, import_south, import_west, import_east, polygon);
		const std::vector<float> edge_distance = terrainSelectionEdgeDistance(selection_mask);

		const int tile_count = range.tile_count;
		std::map<std::pair<int, int>, QImage> tiles;
		int completed = 0;
		for(int tile_y=range.min_y; tile_y<=range.max_y; ++tile_y)
		for(int tile_x=range.min_x; tile_x<=range.max_x; ++tile_x)
		{
			if(isInterruptionRequested())
				throw glare::Exception("elevation import cancelled");
			const std::string url = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/" +
				toString(zoom) + "/" + toString(tile_x) + "/" + toString(tile_y) + ".png";
			std::vector<uint8> response_data;
			const HTTPClient::ResponseInfo response = client.downloadFile(url, response_data);
			if(response.response_code != 200)
				throw glare::Exception("elevation tile request failed with HTTP " + toString(response.response_code));
			QImage tile;
			if(!tile.loadFromData(response_data.data(), (uint)response_data.size(), "PNG") || tile.width() != 256 || tile.height() != 256)
				throw glare::Exception("an elevation tile could not be decoded");
			tiles[std::make_pair(tile_x, tile_y)] = tile.convertToFormat(QImage::Format_RGBA8888);
			emit tileProgress(++completed, tile_count);
		}

		QByteArray heights(REAL_TERRAIN_MAP_RESOLUTION * REAL_TERRAIN_MAP_RESOLUTION * (int)sizeof(float), Qt::Uninitialized);
		float* height_values = reinterpret_cast<float*>(heights.data());
		const int tile_limit = (1 << zoom) - 1;
		for(int py=0; py<REAL_TERRAIN_MAP_RESOLUTION; ++py)
		{
			const uchar* selection_row = selection_mask.constScanLine(py);
			const double v = (double)py / (double)(REAL_TERRAIN_MAP_RESOLUTION - 1);
			const double sample_lat = import_north + (import_south - import_north) * v;
			const double mercator_y = realTerrainMercatorY(sample_lat, zoom) * 256.0;
			const int tile_y = myClamp((int)std::floor(mercator_y / 256.0), 0, tile_limit);
			const int pixel_y = myClamp((int)std::floor(mercator_y - tile_y * 256.0), 0, 255);
			for(int px=0; px<REAL_TERRAIN_MAP_RESOLUTION; ++px)
			{
				const double u = (double)px / (double)(REAL_TERRAIN_MAP_RESOLUTION - 1);
				const double sample_lon = import_west + (import_east - import_west) * u;
				const bool inside_selection = selection_row[px] >= 128;
				if(!inside_selection)
				{
					height_values[py * REAL_TERRAIN_MAP_RESOLUTION + px] = water_z;
					continue;
				}
				const double mercator_x = realTerrainMercatorX(sample_lon, zoom) * 256.0;
				const int tile_x = myClamp((int)std::floor(mercator_x / 256.0), 0, tile_limit);
				const int pixel_x = myClamp((int)std::floor(mercator_x - tile_x * 256.0), 0, 255);
				const auto tile_it = tiles.find(std::make_pair(tile_x, tile_y));
				if(tile_it == tiles.end())
					throw glare::Exception("elevation tile coverage is incomplete");
				const uchar* rgba = tile_it->second.constScanLine(pixel_y) + pixel_x * 4;
				const float source_height = (float)(rgba[0] * 256.0 + rgba[1] + rgba[2] / 256.0 - 32768.0);
				const double shoreline = myClamp(edge_distance[(size_t)py*REAL_TERRAIN_MAP_RESOLUTION+px] / 5.0, 0.0, 1.0);
				const double smooth_shoreline = shoreline * shoreline * (3.0 - 2.0 * shoreline);
				height_values[py * REAL_TERRAIN_MAP_RESOLUTION + px] = (float)(water_z + (source_height - water_z) * smooth_shoreline);
			}
		}

		emit statusMessage(QCoreApplication::translate("WorldSettingsWidget", "Generating material, tree and OSM masks…"));
		QByteArray material_png, tree_png, roads_png, buildings_png;
		const QString feature_status = queryTerrainOSMFeatures(client, import_north, import_south, import_west, import_east,
			polygon, heights, water_z, material_png, tree_png, roads_png, buildings_png);
		{
			QMutexLocker lock(&client_mutex);
			active_client = NULL;
		}
		emit importReady(heights, material_png, tree_png, roads_png, buildings_png, feature_status);
	}
	catch(const glare::Exception& e)
	{
		{
			QMutexLocker lock(&client_mutex);
			active_client = NULL;
		}
		emit importFailed(QString::fromStdString(e.what()));
	}
	catch(const std::exception& e)
	{
		{
			QMutexLocker lock(&client_mutex);
			active_client = NULL;
		}
		emit importFailed(QString::fromUtf8(e.what()));
	}
}


WorldSettingsWidget::WorldSettingsWidget(QWidget* parent)
:	QWidget(parent),
	main_window(NULL),
	terrain_section_resize_drag_active(false),
	terrain_section_resize_drag_start_global_y(0),
	terrain_section_resize_drag_start_height(0),
	terrain_material_browser_target(-1),
	settings_tabs(NULL),
	sculpting_tab(NULL),
	sculpting_basic_accordion_button(NULL),
	sculpting_basic_panel(NULL),
	sculpting_shape_accordion_button(NULL),
	sculpting_shape_panel(NULL),
	sculpting_stamp_accordion_button(NULL),
	sculpting_stamp_panel(NULL),
	sculpting_mode_check_box(NULL),
	sculpting_map_target_combo(NULL),
	sculpting_map_target_label(NULL),
	sculpting_layers_accordion_button(NULL),
	sculpting_layers_group(NULL),
	sculpting_layers_list(NULL),
	sculpting_radius_spin_box(NULL),
	sculpting_strength_spin_box(NULL),
	sculpting_target_height_spin_box(NULL),
	sculpting_island_settings_group(NULL),
	sculpting_island_sea_floor_label(NULL),
	sculpting_island_land_base_label(NULL),
	sculpting_island_peak_label(NULL),
	sculpting_island_seed_label(NULL),
	sculpting_island_sea_floor_slider(NULL),
	sculpting_island_land_base_slider(NULL),
	sculpting_island_peak_slider(NULL),
	sculpting_island_seed_slider(NULL),
	sculpting_island_sea_floor_value(NULL),
	sculpting_island_land_base_value(NULL),
	sculpting_island_peak_value(NULL),
	sculpting_island_seed_value(NULL),
	sculpting_island_preview_label(NULL),
	sculpting_island_preview_image(NULL),
	sculpting_island_random_seed_button(NULL),
	sculpting_island_regenerate_button(NULL),
	sculpting_island_place_button(NULL),
	sculpting_radius_label(NULL),
	sculpting_strength_label(NULL),
	sculpting_target_height_label(NULL),
	sculpting_undo_button(NULL),
	sculpting_redo_button(NULL),
	sculpting_status_label(NULL),
	real_terrain_group(NULL),
	real_terrain_map_label(NULL),
	real_terrain_basemap_combo(NULL),
	real_terrain_section_x_label(NULL),
	real_terrain_section_y_label(NULL),
	real_terrain_source_label(NULL),
	real_terrain_attribution_label(NULL),
	real_terrain_map_widget(NULL),
	real_terrain_rectangle_button(NULL),
	real_terrain_freehand_button(NULL),
	real_terrain_clear_button(NULL),
	real_terrain_zoom_out_button(NULL),
	real_terrain_zoom_in_button(NULL),
	real_terrain_section_x_spin_box(NULL),
	real_terrain_section_y_spin_box(NULL),
	real_terrain_import_button(NULL),
	real_terrain_status_label(NULL),
	real_terrain_import_thread(NULL)
{
	setupUi(this);
	for(int i=0; i<4; ++i)
	{
		terrain_material_preview_labels[i] = new QLabel(this);
		terrain_material_preview_labels[i]->setFixedSize(56, 38);
		terrain_material_preview_labels[i]->setAlignment(Qt::AlignCenter);
		terrain_material_preview_labels[i]->setFrameShape(QFrame::StyledPanel);
		terrain_material_preview_labels[i]->setToolTip(tr("Preview of the terrain material texture"));
		QHBoxLayout* row_layout = findChild<QHBoxLayout*>(QString("detailColMapURLs%1RowLayout").arg(i));
		FileSelectWidget* file_widget = findChild<FileSelectWidget*>(QString("detailColMapURLs%1FileSelectWidget").arg(i));
		if(row_layout && file_widget)
		{
			row_layout->insertWidget(1, terrain_material_preview_labels[i]);
			QPushButton* browser_button = new QPushButton(tr("Choose material"), this);
			browser_button->setObjectName(QString("terrainMaterialBrowserButton%1").arg(i));
			browser_button->setToolTip(tr("Open the material browser and assign its colour texture to this terrain layer."));
			row_layout->addWidget(browser_button);
			QPushButton* poly_haven_button = new QPushButton(tr("Poly Haven"), this);
			poly_haven_button->setObjectName(QString("terrainPolyHavenButton%1").arg(i));
			poly_haven_button->setToolTip(tr("Search Poly Haven textures, preview the albedo, and assign it to this layer."));
			row_layout->addWidget(poly_haven_button);
			connect(poly_haven_button, &QPushButton::clicked, this, [this, i]() { browsePolyHavenTerrainTexture(i); });
			connect(browser_button, &QPushButton::clicked, this, [this, i]()
			{
				terrain_material_browser_target = i;
				if(main_window)
				{
					QDockWidget* dock = main_window->findChild<QDockWidget*>("materialBrowserDockWidget");
					if(dock) { dock->show(); dock->raise(); }
				}
			});
			connect(file_widget, &FileSelectWidget::filenameChanged, this, [this, i](QString&) { updateTerrainMaterialPreview(i); });
		}
		updateTerrainMaterialPreview(i);
	}
	detailColMapURLs0FileSelectWidget->setToolTip(tr("Base-colour texture for the rock material layer. Paint the red channel of the terrain material mask to apply it."));
	detailColMapURLs1FileSelectWidget->setToolTip(tr("Base-colour texture for the ground / sand material layer. Paint the green channel of the terrain material mask to apply it."));
	detailColMapURLs2FileSelectWidget->setToolTip(tr("Base-colour texture for the vegetation material layer. Paint the blue channel of the terrain material mask to apply it."));
	detailColMapURLs3FileSelectWidget->setToolTip(tr("Base-colour texture for the independent overlay layer. Paint the Overlay material layer to apply it over rock, ground and vegetation."));
	for(int i=0; i<17; ++i)
		sculpting_tool_buttons[i] = NULL;
	for(int i=0; i<21; ++i)
		sculpting_shape_tool_buttons[i] = NULL;
	for(int i=0; i<16; ++i)
		sculpting_stamp_tool_buttons[i] = NULL;
	createSculptingTab();

	connect(this->newTerrainSectionPushButton, SIGNAL(clicked()), this, SLOT(newTerrainSectionPushButtonClicked()));

	connect(this->applyPushButton, SIGNAL(clicked()), this, SLOT(applySettingsSlot()));

	this->waterZDoubleSpinBox->setMinimum(-std::numeric_limits<double>::infinity());
	this->waterZDoubleSpinBox->setMaximum( std::numeric_limits<double>::infinity());

	this->defaultTerrainZDoubleSpinBox->setMinimum(-std::numeric_limits<double>::infinity());
	this->defaultTerrainZDoubleSpinBox->setMaximum( std::numeric_limits<double>::infinity());

	connect(this->sunThetaSettingRealControl,   SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->sunPhiSettingRealControl,     SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->layer0ASpinBox,               SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->layer0HeightScaleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->layer1ASpinBox,               SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->layer1HeightScaleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->volumetricCloudsEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudBottomZWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudTopZWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudCoverageWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudDensityWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudWindSpeedWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudBottomDarknessWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudEdgeSoftnessWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudHorizonFadeWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudShapePeriodWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudDetailPeriodWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudMaxMarchDistWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudWindDirectionWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudDirectSunWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudSkyLightWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudSunsetResponseWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudGroundContributionWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudPhaseGWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudPhaseBlendWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudMultiScatteringWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudScatteringWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterCloudReflectionEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	connect(this->cloudWaterReflectionWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterCloudReflectionSamplesWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterCloudReflectionFadeWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterWaveAmplitudeWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterWaveLengthWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterWaveSteepnessWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterWaveSpeedWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterWaveDirectionWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterWaveSpreadWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterSecondaryWaveScaleWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterSurfEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(waterSurfEnabledToggledSlot(bool)));
	connect(this->waterCausticsEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	waterCausticsEnabledCheckBox->setToolTip(tr("Show or hide projected underwater caustics on terrain below the water level. This does not turn the water itself off."));
	connect(this->waterSurfStrengthWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterShorelineWidthWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterFoamScaleWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterFoamSpeedWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->waterFoamFadeWorldRealControl, SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->detailColMapURLs0EnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	connect(this->detailColMapURLs1EnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	connect(this->detailColMapURLs2EnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	connect(this->detailColMapURLs3EnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	connect(this->detailHeightMapURLs0EnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));

	terrainSectionScrollArea->setMouseTracking(true);
	terrainSectionScrollArea->viewport()->setMouseTracking(true);
	terrainSectionScrollArea->installEventFilter(this);
	terrainSectionScrollArea->viewport()->installEventFilter(this);
}


void WorldSettingsWidget::updateTerrainMaterialPreview(int index)
{
	if(index < 0 || index >= 4 || !terrain_material_preview_labels[index])
		return;
	FileSelectWidget* file_widget = findChild<FileSelectWidget*>(QString("detailColMapURLs%1FileSelectWidget").arg(index));
	if(!file_widget)
		return;
	QString image_path = file_widget->filename();
	if(!image_path.isEmpty() && !QFile::exists(image_path) && main_window)
	{
		const std::string resource_path = main_window->gui_client.resource_manager->pathForURL(toURLString(QtUtils::toStdString(image_path)));
		image_path = QtUtils::toQString(resource_path);
	}
	QPixmap preview;
	if(!image_path.isEmpty())
		preview.load(image_path);
	if(!preview.isNull())
		terrain_material_preview_labels[index]->setPixmap(preview.scaled(52, 34, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	else
	{
		terrain_material_preview_labels[index]->setPixmap(QPixmap());
		terrain_material_preview_labels[index]->setText(tr("No preview"));
	}
}


bool WorldSettingsWidget::setTerrainMaterialFromBrowser(const std::string& material_path)
{
	if(terrain_material_browser_target < 0 || terrain_material_browser_target >= 4)
		return false;
	try
	{
		const WorldMaterialRef material = WorldMaterial::loadFromXMLOnDisk(material_path, /*convert_rel_paths_to_abs_disk_paths=*/true);
		if(material.isNull() || material->colour_texture_url.empty())
		{
			QMessageBox::information(this, tr("Terrain material"), tr("This material does not have a colour texture to assign."));
			terrain_material_browser_target = -1;
			return true;
		}
		FileSelectWidget* file_widget = findChild<FileSelectWidget*>(QString("detailColMapURLs%1FileSelectWidget").arg(terrain_material_browser_target));
		if(!file_widget)
		{
			terrain_material_browser_target = -1;
			return true;
		}
		file_widget->setFilename(QtUtils::toQString(material->colour_texture_url));
		updateTerrainMaterialPreview(terrain_material_browser_target);
		terrain_material_browser_target = -1;
		settingsChangedSlot();
		return true;
	}
	catch(const std::exception& e)
	{
		QMessageBox::warning(this, tr("Terrain material"), QString::fromUtf8(e.what()));
		terrain_material_browser_target = -1;
		return true;
	}
}


void WorldSettingsWidget::browsePolyHavenTerrainTexture(int index)
{
	if(!main_window || index < 0 || index >= 4)
		return;
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Poly Haven terrain textures"));
	dialog.resize(620, 480);
	QVBoxLayout* layout = new QVBoxLayout(&dialog);
	QLabel* help = new QLabel(tr("Search CC0 textures. Select a result to load its colour map preview, then assign it to this terrain layer."), &dialog);
	help->setWordWrap(true);
	layout->addWidget(help);
	QHBoxLayout* search_layout = new QHBoxLayout();
	QLineEdit* query_edit = new QLineEdit(&dialog);
	query_edit->setPlaceholderText(tr("English terms, for example beach sand, rock, grass"));
	QPushButton* search_button = new QPushButton(tr("Search"), &dialog);
	search_layout->addWidget(query_edit, 1);
	search_layout->addWidget(search_button);
	layout->addLayout(search_layout);
	QListWidget* results = new QListWidget(&dialog);
	layout->addWidget(results, 1);
	QHBoxLayout* preview_row = new QHBoxLayout();
	QLabel* preview = new QLabel(tr("Select a texture to preview"), &dialog);
	preview->setMinimumSize(192, 128);
	preview->setMaximumSize(192, 128);
	preview->setAlignment(Qt::AlignCenter);
	preview->setFrameShape(QFrame::StyledPanel);
	QLabel* status = new QLabel(&dialog);
	status->setWordWrap(true);
	preview_row->addWidget(preview);
	preview_row->addWidget(status, 1);
	layout->addLayout(preview_row);
	QHBoxLayout* buttons = new QHBoxLayout();
	QPushButton* apply = new QPushButton(tr("Use this texture"), &dialog);
	QPushButton* close = new QPushButton(tr("Cancel"), &dialog);
	apply->setEnabled(false);
	buttons->addStretch(1);
	buttons->addWidget(apply);
	buttons->addWidget(close);
	layout->addLayout(buttons);

	const std::shared_ptr<QHash<QString, QString>> downloaded_paths(new QHash<QString, QString>());
	int request_serial = 0;
	QString selected_id;
	connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
	connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept);
	connect(search_button, &QPushButton::clicked, &dialog, [&dialog, query_edit, results, status, search_button, &request_serial]()
	{
		const QString query = query_edit->text().trimmed();
		if(query.isEmpty())
		{
			status->setText(WorldSettingsWidget::tr("Enter search terms first."));
			return;
		}
		const int serial = ++request_serial;
		results->clear();
		status->setText(WorldSettingsWidget::tr("Searching Poly Haven…"));
		search_button->setEnabled(false);
		QPointer<QDialog> dialog_guard(&dialog);
		QPointer<QListWidget> results_guard(results);
		QPointer<QLabel> status_guard(status);
		QPointer<QPushButton> search_guard(search_button);
		QThread* worker = QThread::create([query, serial, dialog_guard, results_guard, status_guard, search_guard]()
		{
			QJsonArray assets;
			QString error;
			try { assets = MCPTextures::search(QJsonObject{{"query", query}, {"limit", 24}}).value("assets").toArray(); }
			catch(const std::exception& e) { error = QString::fromUtf8(e.what()); }
			QMetaObject::invokeMethod(QCoreApplication::instance(), [serial, assets, error, dialog_guard, results_guard, status_guard, search_guard]()
			{
				if(!dialog_guard || !results_guard || !status_guard || !search_guard)
					return;
				if(serial != dialog_guard->property("polyHavenSearchSerial").toInt())
					return;
				search_guard->setEnabled(true);
				if(!error.isEmpty()) { status_guard->setText(error); return; }
				for(const QJsonValue& value : assets)
				{
					const QJsonObject asset = value.toObject();
					QListWidgetItem* item = new QListWidgetItem(QString("%1  —  %2").arg(asset.value("name").toString(), asset.value("category").toString()), results_guard);
					item->setData(Qt::UserRole, asset.value("id").toString());
				}
				status_guard->setText(assets.isEmpty() ? WorldSettingsWidget::tr("No matching textures found.") : WorldSettingsWidget::tr("Select a result to load its colour texture preview."));
			});
		});
		QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
		worker->start();
		dialog.setProperty("polyHavenSearchSerial", serial);
	});
	connect(query_edit, &QLineEdit::returnPressed, search_button, &QPushButton::click);
	connect(results, &QListWidget::currentItemChanged, &dialog, [this, index, &dialog, results, preview, status, apply, downloaded_paths, &selected_id](QListWidgetItem* current)
	{
		if(!current)
			return;
		const QString id = current->data(Qt::UserRole).toString();
		selected_id = id;
		if(downloaded_paths->contains(id))
		{
			QPixmap image(downloaded_paths->value(id));
			preview->setPixmap(image.scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
			status->setText(current->text() + tr("\nLicense: CC0 — Poly Haven"));
			apply->setEnabled(true);
			return;
		}
		preview->setText(tr("Loading colour map preview…"));
		preview->setPixmap(QPixmap());
		status->setText(current->text());
		apply->setEnabled(false);
		const std::string cache_dir = main_window->appdata_path + "/terrain_texture_cache";
		QPointer<QDialog> dialog_guard(&dialog);
		QPointer<QLabel> preview_guard(preview);
		QPointer<QLabel> status_guard(status);
		QPointer<QPushButton> apply_guard(apply);
		QThread* worker = QThread::create([id, cache_dir, dialog_guard, preview_guard, status_guard, apply_guard, downloaded_paths]()
		{
			QString path, error;
			QImage image;
			try
			{
				const QJsonObject files = QJsonDocument::fromJson(MCPTextures::fetch(QUrl(QString("https://api.polyhaven.com/files/%1").arg(id)))).object();
				image = MCPTextures::map(files, {"Diffuse", "diff", "diffuse", "Color"}, "1k", true);
				QDir().mkpath(QtUtils::toQString(cache_dir));
				path = QtUtils::toQString(cache_dir + "/polyhaven_" + QtUtils::toStdString(id) + "_1k.png");
				if(!image.save(path, "PNG")) throw glare::Exception("Could not save the Poly Haven preview image.");
			}
			catch(const std::exception& e) { error = QString::fromUtf8(e.what()); }
			QMetaObject::invokeMethod(QCoreApplication::instance(), [id, image, path, error, dialog_guard, preview_guard, status_guard, apply_guard, downloaded_paths]()
			{
				if(!dialog_guard || !preview_guard || !status_guard || !apply_guard || dialog_guard->property("polyHavenSelectedId").toString() != id)
					return;
				if(!error.isEmpty()) { status_guard->setText(error); preview_guard->setText(WorldSettingsWidget::tr("Preview unavailable")); return; }
				downloaded_paths->insert(id, path);
				preview_guard->setPixmap(QPixmap::fromImage(image).scaled(preview_guard->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
				status_guard->setText(WorldSettingsWidget::tr("%1\nLicense: CC0 — Poly Haven").arg(id));
				apply_guard->setEnabled(true);
			});
		});
		QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
		worker->start();
		dialog.setProperty("polyHavenSelectedId", id);
	});
	query_edit->setText("sand");
	search_button->click();
	if(dialog.exec() == QDialog::Accepted && downloaded_paths->contains(selected_id))
	{
		FileSelectWidget* file_widget = findChild<FileSelectWidget*>(QString("detailColMapURLs%1FileSelectWidget").arg(index));
		if(file_widget)
		{
			file_widget->setFilename(downloaded_paths->value(selected_id));
			updateTerrainMaterialPreview(index);
			settingsChangedSlot();
		}
	}
}


void WorldSettingsWidget::createSculptingTab()
{
	// Move the existing generated form into the first tab without changing its
	// widget object names or the already implemented terrain-map controls.  The
	// generated UI uses a QFormLayout; transferring the layout object itself
	// leaves its child widgets owned by the old top-level widget on Qt 5.  Move
	// each form item instead, preserving its row/role and therefore the complete
	// original world-settings form.
	QLayout* existing_layout = this->layout();
	QWidget* world_tab = new QWidget(this);
	QFormLayout* world_form_layout = new QFormLayout(world_tab);
	if(existing_layout)
	{
		QFormLayout* old_form_layout = static_cast<QFormLayout*>(existing_layout);
		world_form_layout->setFieldGrowthPolicy(old_form_layout->fieldGrowthPolicy());
		world_form_layout->setFormAlignment(old_form_layout->formAlignment());
		world_form_layout->setLabelAlignment(old_form_layout->labelAlignment());
		world_form_layout->setHorizontalSpacing(old_form_layout->horizontalSpacing());
		world_form_layout->setVerticalSpacing(old_form_layout->verticalSpacing());
		int left_margin, top_margin, right_margin, bottom_margin;
		old_form_layout->getContentsMargins(&left_margin, &top_margin, &right_margin, &bottom_margin);
		world_form_layout->setContentsMargins(left_margin, top_margin, right_margin, bottom_margin);

		for(int i=old_form_layout->count()-1; i>=0; --i)
		{
			int row;
			QFormLayout::ItemRole role;
			old_form_layout->getItemPosition(i, &row, &role);
			QLayoutItem* item = old_form_layout->takeAt(i);
			if(QWidget* item_widget = item->widget())
				item_widget->setParent(world_tab);
			world_form_layout->setItem(row, role, item);
		}
		delete old_form_layout;
	}

	// The world-settings dock is intentionally resizable.  Long map URLs must
	// not force a fixed minimum width and crop the controls when the dock is
	// narrowed: labels wrap and editable fields consume the available width.
	world_tab->setMinimumWidth(0);
	world_tab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	world_form_layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	world_form_layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	world_form_layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
	for(int i=0; i<world_form_layout->count(); ++i)
	{
		int row;
		QFormLayout::ItemRole role;
		world_form_layout->getItemPosition(i, &row, &role);
		QLayoutItem* item = world_form_layout->itemAt(i);
		QWidget* item_widget = item ? item->widget() : NULL;
		if(!item_widget)
			continue;

		item_widget->setMinimumWidth(0);
		if(role == QFormLayout::LabelRole)
		{
			if(QLabel* label = qobject_cast<QLabel*>(item_widget))
				label->setWordWrap(true);
			item_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
		}
		else
			item_widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	}
	terrainSectionScrollArea->setMinimumWidth(0);
	terrainSectionScrollArea->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	terrainSectionScrollAreaWidgetContents->setMinimumWidth(0);

	settings_tabs = new QTabWidget(this);
	settings_tabs->setMinimumWidth(0);
	settings_tabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	settings_tabs->addTab(world_tab, tr("World"));

	sculpting_tab = new QWidget(settings_tabs);
	QVBoxLayout* sculpting_layout = new QVBoxLayout(sculpting_tab);

	sculpting_mode_check_box = new QCheckBox(sculpting_tab);
	sculpting_mode_check_box->setText(tr("Enable sculpting mode"));
	sculpting_layout->addWidget(sculpting_mode_check_box);

	QHBoxLayout* history_layout = new QHBoxLayout();
	sculpting_undo_button = new QPushButton(sculpting_tab);
	sculpting_undo_button->setText(tr("Undo"));
	sculpting_redo_button = new QPushButton(sculpting_tab);
	sculpting_redo_button->setText(tr("Redo"));
	history_layout->addWidget(sculpting_undo_button);
	history_layout->addWidget(sculpting_redo_button);
	sculpting_layout->addLayout(history_layout);

	QFormLayout* brush_layout = new QFormLayout();
	sculpting_radius_spin_box = new QDoubleSpinBox(sculpting_tab);
	sculpting_radius_spin_box->setRange(0.25, 4096.0);
	sculpting_radius_spin_box->setValue(32.0);
	sculpting_radius_spin_box->setDecimals(2);
	sculpting_radius_spin_box->setSuffix(tr(" m"));
	sculpting_radius_spin_box->setToolTip(tr("Sets the brush radius in metres. Use a smaller radius for detail and a larger radius for broad changes."));
	sculpting_radius_label = new QLabel(tr("Brush radius"), sculpting_tab);
	brush_layout->addRow(sculpting_radius_label, sculpting_radius_spin_box);

	sculpting_strength_spin_box = new QDoubleSpinBox(sculpting_tab);
	sculpting_strength_spin_box->setRange(0.01, 100.0);
	sculpting_strength_spin_box->setValue(1.0);
	sculpting_strength_spin_box->setDecimals(2);
	sculpting_strength_spin_box->setSuffix(tr(" m"));
	sculpting_strength_spin_box->setToolTip(tr("Controls height change in metres. For surface and tree masks it also controls paint opacity: roughly 10% per strength unit."));
	sculpting_strength_label = new QLabel(tr("Strength"), sculpting_tab);
	brush_layout->addRow(sculpting_strength_label, sculpting_strength_spin_box);

	sculpting_target_height_spin_box = new QDoubleSpinBox(sculpting_tab);
	sculpting_target_height_spin_box->setRange(-100000.0, 100000.0);
	sculpting_target_height_spin_box->setValue(0.0);
	sculpting_target_height_spin_box->setDecimals(2);
	sculpting_target_height_spin_box->setSuffix(tr(" m"));
	sculpting_target_height_spin_box->setToolTip(tr("Target elevation used by height tools such as Flatten and Fill to level."));
	sculpting_target_height_label = new QLabel(tr("Target height"), sculpting_tab);
	brush_layout->addRow(sculpting_target_height_label, sculpting_target_height_spin_box);
	sculpting_layout->addLayout(brush_layout);

	sculpting_map_target_label = new QLabel(tr("Edit / generate"), sculpting_tab);
	sculpting_map_target_label->setToolTip(tr("Choose whether strokes sculpt height, paint a surface or vegetation mask, or place an island."));
	sculpting_map_target_combo = new QComboBox(sculpting_tab);
	sculpting_map_target_combo->addItem(tr("Terrain height"), 0);
	sculpting_map_target_combo->addItem(tr("Rock material"), 1);
	sculpting_map_target_combo->addItem(tr("Ground / sand material"), 2);
	sculpting_map_target_combo->addItem(tr("Vegetation material"), 3);
	sculpting_map_target_combo->addItem(tr("Allow trees"), 4);
	sculpting_map_target_combo->addItem(tr("Block trees"), 5);
	sculpting_map_target_combo->addItem(tr("Restore source map"), 6);
	sculpting_map_target_combo->addItem(tr("Overlay material (e.g. beach / snow)"), 7);
	sculpting_map_target_combo->insertSeparator(sculpting_map_target_combo->count());
	const QString island_names[24] = {
		tr("Island: classic volcanic"), tr("Island: high mountainous"), tr("Island: low coral cay"), tr("Island: barrier / long"),
		tr("Island: twin linked"), tr("Island: caldera"), tr("Island: table / mesa"), tr("Island: rias / drowned"),
		tr("Island: spit / sandbar"), tr("Island: arch rocks"), tr("Island: volcanic chain"), tr("Island: reef atoll"),
		tr("Island: ring"), tr("Island: crescent"), tr("Island: star"), tr("Island: spiral"), tr("Island: serpent"),
		tr("Island: shattered"), tr("Island: crystal spires"), tr("Island: cliff plateau"), tr("Island: mushroom cap"),
		tr("Island: heart"), tr("Island: sky needle"), tr("Island: lagoon chain")
	};
	for(int i=0; i<24; ++i)
	{
		if(i == 0 || i == 12)
		{
			sculpting_map_target_combo->addItem(i == 0 ? tr("Real islands") : tr("Fantasy islands"));
			QStandardItemModel* model = qobject_cast<QStandardItemModel*>(sculpting_map_target_combo->model());
			if(model && model->item(sculpting_map_target_combo->count() - 1))
				model->item(sculpting_map_target_combo->count() - 1)->setFlags(Qt::NoItemFlags);
		}
		sculpting_map_target_combo->addItem(island_names[i], 8 + i);
	}
	sculpting_map_target_combo->insertSeparator(sculpting_map_target_combo->count());
	sculpting_map_target_combo->addItem(tr("Paint grass"), 32);
	sculpting_map_target_combo->addItem(tr("Paint trees and grass"), 33);
	sculpting_map_target_combo->setItemIcon(sculpting_map_target_combo->findData(32), makeTerrainLayerPreviewIcon(QColor(72, 150, 58)));
	sculpting_map_target_combo->setItemIcon(sculpting_map_target_combo->findData(33), makeTerrainLayerPreviewIcon(QColor(52, 118, 44)));
	sculpting_map_target_combo->setItemData(sculpting_map_target_combo->findData(32),
		tr("Paint procedural grass by adding vegetation. Trees are blocked in the painted area."), Qt::ToolTipRole);
	sculpting_map_target_combo->setItemData(sculpting_map_target_combo->findData(33),
		tr("Paint procedural grass and allow trees in the painted area."), Qt::ToolTipRole);
	sculpting_map_target_combo->setIconSize(QSize(48, 30));
	const int target_icons[8] = { -1, 0, 1, 2, -1, -1, -1, 3 };
	const QString target_tooltips[8] = {
		tr("Sculpt terrain height; click or drag across the terrain."),
		tr("Paint the rock surface weight. Assign a rock texture in World settings to see the material."),
		tr("Paint the ground and sand surface weight. Assign a texture in World settings to see the material."),
		tr("Paint the vegetation surface weight. This mask also controls terrain vegetation scattering."),
		tr("Paint the tree placement mask to allow trees in this area."),
		tr("Paint the tree placement mask to block trees in this area."),
		tr("Restore the imported surface and tree masks in the brushed area."),
		tr("Paint the independent overlay texture, such as beach, snow, or rock." )
	};
	for(int i=0; i<8; ++i)
	{
		const int index = sculpting_map_target_combo->findData(i);
		if(index < 0) continue;
		if(target_icons[i] >= 0)
			sculpting_map_target_combo->setItemIcon(index, makeTerrainLayerPreviewIcon(QColor(116,112,106)));
		sculpting_map_target_combo->setItemData(index, target_tooltips[i], Qt::ToolTipRole);
	}
	if(sculpting_map_target_combo->findData(0) >= 0)
		sculpting_map_target_combo->setItemIcon(sculpting_map_target_combo->findData(0), makeSculptToolPreviewIcon(0));
	for(int i=4; i<=6; ++i)
	{
		const int index = sculpting_map_target_combo->findData(i);
		if(index >= 0)
			sculpting_map_target_combo->setItemIcon(index, makeTerrainLayerPreviewIcon(i == 4 ? QColor(72,112,48) : i == 5 ? QColor(70,72,76) : QColor(100,160,210)));
	}
	for(int i=0; i<24; ++i)
	{
		const int index = sculpting_map_target_combo->findData(8 + i);
		if(index >= 0)
		{
			sculpting_map_target_combo->setItemIcon(index, makeSculptToolPreviewIcon(17 + i));
			sculpting_map_target_combo->setItemData(index, tr("Preview and place this island shape. Click Add island to world, then click the location in the scene."), Qt::ToolTipRole);
		}
	}
	sculpting_map_target_combo->setItemIcon(sculpting_map_target_combo->findData(1), makeTerrainLayerPreviewIcon(QColor(116,112,106)));
	sculpting_map_target_combo->setItemIcon(sculpting_map_target_combo->findData(2), makeTerrainLayerPreviewIcon(QColor(174,145,96)));
	sculpting_map_target_combo->setItemIcon(sculpting_map_target_combo->findData(3), makeTerrainLayerPreviewIcon(QColor(72,112,48)));
	sculpting_map_target_combo->setItemIcon(sculpting_map_target_combo->findData(7), makeTerrainLayerPreviewIcon(QColor(232,238,246)));
	const int real_island_item = sculpting_map_target_combo->findData(8);
	const int fantasy_island_item = sculpting_map_target_combo->findData(20);
	if(real_island_item > 0) sculpting_map_target_combo->setItemText(real_island_item - 1, tr("Real islands"));
	if(fantasy_island_item > 0) sculpting_map_target_combo->setItemText(fantasy_island_item - 1, tr("Fantasy islands"));
	QFormLayout* map_target_layout = new QFormLayout();
	map_target_layout->addRow(sculpting_map_target_label, sculpting_map_target_combo);
	sculpting_layout->addLayout(map_target_layout);

	sculpting_layers_accordion_button = new QToolButton(sculpting_tab);
	sculpting_layers_accordion_button->setText(tr("Surface layers"));
	sculpting_layers_accordion_button->setCheckable(true);
	sculpting_layers_accordion_button->setChecked(true);
	sculpting_layers_accordion_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	sculpting_layers_accordion_button->setArrowType(Qt::DownArrow);
	sculpting_layers_accordion_button->setToolTip(tr("Expand or collapse the paintable terrain material layers."));
	sculpting_layout->addWidget(sculpting_layers_accordion_button);
	sculpting_layers_group = new QGroupBox(sculpting_tab);
	QVBoxLayout* sculpting_layers_layout = new QVBoxLayout(sculpting_layers_group);
	sculpting_layers_list = new QListWidget(sculpting_layers_group);
	sculpting_layers_list->setSelectionMode(QAbstractItemView::SingleSelection);
	sculpting_layers_list->setIconSize(QSize(44, 30));
	sculpting_layers_list->setMaximumHeight(142);
	sculpting_layers_list->setToolTip(tr("Choose the material layer to paint on the terrain. The brush changes its weight map; it does not replace the terrain height map."));
	const QString material_layer_names[4] = { tr("Rock"), tr("Ground / sand"), tr("Vegetation"), tr("Overlay material") };
	const QColor material_layer_colours[4] = { QColor(116, 112, 106), QColor(174, 145, 96), QColor(72, 112, 48), QColor(232, 238, 246) };
	for(int i=0; i<4; ++i)
	{
		QListWidgetItem* item = new QListWidgetItem(makeTerrainLayerPreviewIcon(material_layer_colours[i]), material_layer_names[i]);
		item->setData(Qt::UserRole, i == 3 ? 7 : i + 1);
		item->setToolTip(i == 0 ? tr("Paint the rock weight map. The assigned rock texture appears where this layer has weight.") :
			i == 1 ? tr("Paint the ground / sand weight map. The assigned ground texture appears where this layer has weight.") :
			i == 2 ? tr("Paint the vegetation weight map. Tree placement also depends on the separate tree mask.") :
			tr("Paint an independent overlay, such as beach or snow. This layer blends over the three base materials."));
		item->setSizeHint(QSize(0, 34));
		item->setToolTip(item->toolTip() + tr(" Surface texture is assigned in the World tab. Select this row, then paint in the scene."));
		sculpting_layers_list->addItem(item);
	}
	sculpting_layers_layout->addWidget(sculpting_layers_list);
	sculpting_layout->addWidget(sculpting_layers_group);

	sculpting_island_settings_group = new QGroupBox(tr("Island settings"), sculpting_tab);
	QGridLayout* island_settings_layout = new QGridLayout(sculpting_island_settings_group);
	auto addIslandSetting = [&](int row, const QString& label_text, int minimum, int maximum, int initial,
		QLabel*& setting_label, QSlider*& slider, QLabel*& value)
	{
		setting_label = new QLabel(label_text, sculpting_island_settings_group);
		slider = new QSlider(Qt::Horizontal, sculpting_island_settings_group);
		slider->setRange(minimum, maximum);
		slider->setValue(initial);
		value = new QLabel(sculpting_island_settings_group);
		value->setMinimumWidth(54);
		island_settings_layout->addWidget(setting_label, row, 0);
		island_settings_layout->addWidget(slider, row, 1);
		island_settings_layout->addWidget(value, row, 2);
	};
	addIslandSetting(0, tr("Sea floor"), -160, -10, -85, sculpting_island_sea_floor_label, sculpting_island_sea_floor_slider, sculpting_island_sea_floor_value);
	addIslandSetting(1, tr("Land base"), 0, 50, 8, sculpting_island_land_base_label, sculpting_island_land_base_slider, sculpting_island_land_base_value);
	addIslandSetting(2, tr("Maximum peak"), 60, 1200, 282, sculpting_island_peak_label, sculpting_island_peak_slider, sculpting_island_peak_value);
	addIslandSetting(3, tr("Seed"), 1, 9999, 42, sculpting_island_seed_label, sculpting_island_seed_slider, sculpting_island_seed_value);
	sculpting_island_preview_label = new QLabel(tr("Island preview"), sculpting_island_settings_group);
	sculpting_island_preview_image = new QLabel(sculpting_island_settings_group);
	sculpting_island_preview_image->setMinimumHeight(108);
	sculpting_island_preview_image->setMinimumWidth(220);
	sculpting_island_preview_image->setAlignment(Qt::AlignCenter);
	island_settings_layout->addWidget(sculpting_island_preview_label, 4, 0, 1, 3);
	island_settings_layout->addWidget(sculpting_island_preview_image, 5, 0, 1, 3);
	QHBoxLayout* island_buttons_layout = new QHBoxLayout();
	sculpting_island_random_seed_button = new QPushButton(tr("Randomize seed"), sculpting_island_settings_group);
	sculpting_island_regenerate_button = new QPushButton(tr("Regenerate island"), sculpting_island_settings_group);
	sculpting_island_place_button = new QPushButton(tr("Add island to world"), sculpting_island_settings_group);
	island_buttons_layout->addWidget(sculpting_island_random_seed_button);
	island_buttons_layout->addWidget(sculpting_island_regenerate_button);
	island_settings_layout->addLayout(island_buttons_layout, 6, 0, 1, 3);
	island_settings_layout->addWidget(sculpting_island_place_button, 7, 0, 1, 3);
	sculpting_island_settings_group->setVisible(false);
	sculpting_layout->addWidget(sculpting_island_settings_group);

	sculpting_basic_accordion_button = new QToolButton(sculpting_tab);
	sculpting_basic_accordion_button->setText(tr("Basic tools"));
	sculpting_basic_accordion_button->setCheckable(true);
	sculpting_basic_accordion_button->setChecked(true);
	sculpting_basic_accordion_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	sculpting_basic_accordion_button->setArrowType(Qt::DownArrow);
	sculpting_layout->addWidget(sculpting_basic_accordion_button);

	sculpting_basic_panel = new QWidget(sculpting_tab);
	QGridLayout* basic_grid = new QGridLayout(sculpting_basic_panel);
	const QString tool_names[17] = {
		tr("Ridge"), tr("Raise"), tr("Lower"), tr("Soft raise"), tr("Hard raise"),
		tr("Soft lower"), tr("Hard lower"), tr("Pinch"), tr("Inflate"), tr("Deflate"),
		tr("Clay build"), tr("Blob"), tr("Amplify"), tr("Dampen"), tr("Smooth"),
		tr("Polish"), tr("Sharpen")
	};
	for(int i=0; i<17; ++i)
	{
		sculpting_tool_buttons[i] = new QPushButton(sculpting_basic_panel);
		sculpting_tool_buttons[i]->setText(tool_names[i]);
		sculpting_tool_buttons[i]->setIcon(makeSculptToolPreviewIcon(i));
		sculpting_tool_buttons[i]->setIconSize(QSize(48, 30));
		sculpting_tool_buttons[i]->setToolTip(tr("%1: select the brush, then left-drag on the terrain. The thumbnail shows the approximate height profile; radius and strength are set above.").arg(tool_names[i]));
		sculpting_tool_buttons[i]->setStyleSheet(QStringLiteral("text-align: left; padding-left: 8px;"));
		sculpting_tool_buttons[i]->setCheckable(true);
		sculpting_tool_buttons[i]->setAutoExclusive(true);
		sculpting_tool_buttons[i]->setProperty("sculptTool", i);
		basic_grid->addWidget(sculpting_tool_buttons[i], i / 2, i % 2);
		connect(sculpting_tool_buttons[i], &QPushButton::clicked, this, [this, i]()
		{
			for(int j=0; j<21; ++j) sculpting_shape_tool_buttons[j]->setChecked(false);
			for(int j=0; j<16; ++j) sculpting_stamp_tool_buttons[j]->setChecked(false);
			emit sculptingToolChangedSignal(i);
		});
	}
	sculpting_tool_buttons[0]->setChecked(true);
	sculpting_layout->addWidget(sculpting_basic_panel);

	sculpting_shape_accordion_button = new QToolButton(sculpting_tab);
	sculpting_shape_accordion_button->setText(tr("Shape tools"));
	sculpting_shape_accordion_button->setCheckable(true);
	sculpting_shape_accordion_button->setChecked(false);
	sculpting_shape_accordion_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	sculpting_shape_accordion_button->setArrowType(Qt::RightArrow);
	sculpting_layout->addWidget(sculpting_shape_accordion_button);

	sculpting_shape_panel = new QWidget(sculpting_tab);
	QGridLayout* shape_grid = new QGridLayout(sculpting_shape_panel);
	const QString shape_tool_names[21] = {
		tr("Flatten"), tr("Plateau"), tr("Ramp"), tr("Cliff"), tr("Wall ridge"),
		tr("Basin"), tr("Gorge"), tr("Berm"), tr("Saddle"), tr("Notch"),
		tr("Lake"), tr("Fill level"), tr("Lowland"), tr("Coast / beach"), tr("Shelf"),
		tr("Cove"), tr("Spit"), tr("River / valley"), tr("Channel"), tr("Delta"), tr("Carve sea")
	};
	for(int i=0; i<21; ++i)
	{
		sculpting_shape_tool_buttons[i] = new QPushButton(sculpting_shape_panel);
		sculpting_shape_tool_buttons[i]->setText(shape_tool_names[i]);
		sculpting_shape_tool_buttons[i]->setIcon(makeSculptToolPreviewIcon(17 + i));
		sculpting_shape_tool_buttons[i]->setIconSize(QSize(48, 30));
		sculpting_shape_tool_buttons[i]->setToolTip(tr("%1: select the shape, set the target height if needed, then left-drag on the terrain. The thumbnail shows its approximate profile.").arg(shape_tool_names[i]));
		sculpting_shape_tool_buttons[i]->setStyleSheet(QStringLiteral("text-align: left; padding-left: 8px;"));
		sculpting_shape_tool_buttons[i]->setCheckable(true);
		sculpting_shape_tool_buttons[i]->setAutoExclusive(true);
		sculpting_shape_tool_buttons[i]->setProperty("sculptTool", 17 + i);
		shape_grid->addWidget(sculpting_shape_tool_buttons[i], i / 2, i % 2);
		connect(sculpting_shape_tool_buttons[i], &QPushButton::clicked, this, [this, i]()
		{
			for(int j=0; j<17; ++j) sculpting_tool_buttons[j]->setChecked(false);
			for(int j=0; j<16; ++j) sculpting_stamp_tool_buttons[j]->setChecked(false);
			emit sculptingToolChangedSignal(17 + i);
		});
	}
	sculpting_shape_panel->setVisible(false);
	sculpting_layout->addWidget(sculpting_shape_panel);

	sculpting_stamp_accordion_button = new QToolButton(sculpting_tab);
	sculpting_stamp_accordion_button->setText(tr("Stamps"));
	sculpting_stamp_accordion_button->setCheckable(true);
	sculpting_stamp_accordion_button->setChecked(false);
	sculpting_stamp_accordion_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	sculpting_stamp_accordion_button->setArrowType(Qt::RightArrow);
	sculpting_layout->addWidget(sculpting_stamp_accordion_button);

	sculpting_stamp_panel = new QWidget(sculpting_tab);
	QGridLayout* stamp_grid = new QGridLayout(sculpting_stamp_panel);
	const QString stamp_tool_names[16] = {
		tr("Volcano"), tr("Crater"), tr("Hill"), tr("Cone"), tr("Mesa"), tr("Caldera"),
		tr("Ridge stamp"), tr("Spire"), tr("Pyramid"), tr("Bowl"), tr("Arch"), tr("Atoll"),
		tr("Twin hills"), tr("Dunes"), tr("Tor rocks"), tr("Ramp stamp")
	};
	for(int i=0; i<16; ++i)
	{
		sculpting_stamp_tool_buttons[i] = new QPushButton(sculpting_stamp_panel);
		sculpting_stamp_tool_buttons[i]->setText(stamp_tool_names[i]);
		sculpting_stamp_tool_buttons[i]->setIcon(makeSculptToolPreviewIcon(38 + i));
		sculpting_stamp_tool_buttons[i]->setIconSize(QSize(48, 30));
		sculpting_stamp_tool_buttons[i]->setToolTip(tr("%1: select the stamp, then click or drag to apply its height shape. Brush radius controls its size; the thumbnail is schematic.").arg(stamp_tool_names[i]));
		sculpting_stamp_tool_buttons[i]->setStyleSheet(QStringLiteral("text-align: left; padding-left: 8px;"));
		sculpting_stamp_tool_buttons[i]->setCheckable(true);
		sculpting_stamp_tool_buttons[i]->setAutoExclusive(true);
		sculpting_stamp_tool_buttons[i]->setProperty("sculptTool", 38 + i);
		stamp_grid->addWidget(sculpting_stamp_tool_buttons[i], i / 2, i % 2);
		connect(sculpting_stamp_tool_buttons[i], &QPushButton::clicked, this, [this, i]()
		{
			for(int j=0; j<17; ++j) sculpting_tool_buttons[j]->setChecked(false);
			for(int j=0; j<21; ++j) sculpting_shape_tool_buttons[j]->setChecked(false);
			emit sculptingToolChangedSignal(38 + i);
		});
	}
	sculpting_stamp_panel->setVisible(false);
	sculpting_layout->addWidget(sculpting_stamp_panel);

	real_terrain_group = new QGroupBox(tr("Real-world terrain"), sculpting_tab);
	QFormLayout* real_terrain_layout = new QFormLayout(real_terrain_group);
	real_terrain_map_label = new QLabel(tr("Select an area on the map. Drag with the left mouse button; right-drag pans, wheel zooms."), real_terrain_group);
	real_terrain_map_label->setWordWrap(true);
	real_terrain_layout->addRow(real_terrain_map_label);
	real_terrain_basemap_combo = new QComboBox(real_terrain_group);
	real_terrain_basemap_combo->addItem(tr("Metasiberia Map"));
	real_terrain_basemap_combo->addItem(tr("Topographic map"));
	real_terrain_basemap_combo->addItem(tr("Satellite imagery"));
	real_terrain_layout->addRow(tr("Map layer"), real_terrain_basemap_combo);
	real_terrain_map_widget = new RealTerrainMapWidget(real_terrain_group);
	QHBoxLayout* map_zoom_layout = new QHBoxLayout();
	map_zoom_layout->addStretch(1);
	real_terrain_zoom_out_button = new QPushButton(tr("−"), real_terrain_group);
	real_terrain_zoom_out_button->setToolTip(tr("Zoom out"));
	real_terrain_zoom_in_button = new QPushButton(tr("+"), real_terrain_group);
	real_terrain_zoom_in_button->setToolTip(tr("Zoom in"));
	map_zoom_layout->addWidget(real_terrain_zoom_out_button);
	map_zoom_layout->addWidget(real_terrain_zoom_in_button);
	real_terrain_layout->addRow(map_zoom_layout);
	real_terrain_layout->addRow(real_terrain_map_widget);
	real_terrain_attribution_label = new QLabel(real_terrain_group);
	real_terrain_attribution_label->setWordWrap(true);
	real_terrain_attribution_label->setStyleSheet(QStringLiteral("color: #a8a8a8; font-size: 9px;"));
	real_terrain_layout->addRow(real_terrain_attribution_label);
	connect(real_terrain_basemap_combo, qOverload<int>(&QComboBox::currentIndexChanged), real_terrain_map_widget, &RealTerrainMapWidget::setBasemap);
	connect(real_terrain_basemap_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { retranslateSculptingTab(); });
	connect(real_terrain_zoom_out_button, &QPushButton::clicked, this, [this]() { real_terrain_map_widget->zoomBy(-1); });
	connect(real_terrain_zoom_in_button, &QPushButton::clicked, this, [this]() { real_terrain_map_widget->zoomBy(1); });
	QHBoxLayout* map_tool_layout = new QHBoxLayout();
	real_terrain_rectangle_button = new QPushButton(tr("Rectangle"), real_terrain_group);
	real_terrain_freehand_button = new QPushButton(tr("Freehand contour"), real_terrain_group);
	real_terrain_clear_button = new QPushButton(tr("Clear selection"), real_terrain_group);
	map_tool_layout->addWidget(real_terrain_rectangle_button);
	map_tool_layout->addWidget(real_terrain_freehand_button);
	map_tool_layout->addWidget(real_terrain_clear_button);
	real_terrain_layout->addRow(map_tool_layout);
	connect(real_terrain_rectangle_button, &QPushButton::clicked, this, [this]() { real_terrain_map_widget->setFreehand(false); });
	connect(real_terrain_freehand_button, &QPushButton::clicked, this, [this]() { real_terrain_map_widget->setFreehand(true); });
	connect(real_terrain_clear_button, &QPushButton::clicked, real_terrain_map_widget, &RealTerrainMapWidget::clearSelection);
	real_terrain_section_x_label = new QLabel(tr("Target section X"), real_terrain_group);
	real_terrain_section_x_spin_box = new QSpinBox(real_terrain_group);
	real_terrain_section_x_spin_box->setRange(-100, 100);
	real_terrain_section_x_spin_box->setValue(0);
	real_terrain_layout->addRow(real_terrain_section_x_label, real_terrain_section_x_spin_box);
	real_terrain_section_y_label = new QLabel(tr("Target section Y"), real_terrain_group);
	real_terrain_section_y_spin_box = new QSpinBox(real_terrain_group);
	real_terrain_section_y_spin_box->setRange(-100, 100);
	real_terrain_section_y_spin_box->setValue(0);
	real_terrain_layout->addRow(real_terrain_section_y_label, real_terrain_section_y_spin_box);
	real_terrain_import_button = new QPushButton(tr("Import terrain and layers"), real_terrain_group);
	real_terrain_layout->addRow(real_terrain_import_button);
	real_terrain_source_label = new QLabel(tr("Imports height, material, tree, road and building maps into the chosen terrain section. OSM road/building detail is available for areas up to about 25 km across."), real_terrain_group);
	real_terrain_source_label->setWordWrap(true);
	real_terrain_layout->addRow(real_terrain_source_label);
	real_terrain_status_label = new QLabel(real_terrain_group);
	real_terrain_status_label->setWordWrap(true);
	real_terrain_layout->addRow(real_terrain_status_label);
	sculpting_layout->addWidget(real_terrain_group);

	connect(real_terrain_import_button, &QPushButton::clicked, this, [this]()
	{
		if(real_terrain_import_thread || !main_window || !main_window->gui_client.resource_manager)
			return;
		if(!real_terrain_map_widget->hasSelection())
		{
			real_terrain_status_label->setText(tr("Select an area on the map first."));
			return;
		}
		double north, south, west, east;
		real_terrain_map_widget->getSelection(north, south, west, east);
		const QVector<QPointF> polygon = real_terrain_map_widget->getSelectionPolygon();
		if((east - west) < 0.0001 || (north - south) < 0.0001)
		{
			real_terrain_status_label->setText(tr("The selected area is too small. Draw a larger area."));
			return;
		}
		real_terrain_import_button->setEnabled(false);
		real_terrain_status_label->setText(tr("Starting elevation download…"));
		RealTerrainImportThread* worker = new RealTerrainImportThread(
			north, south, west, east, polygon, (float)waterZDoubleSpinBox->value(), this);
		real_terrain_import_thread = worker;
		connect(worker, &RealTerrainImportThread::tileProgress, this, [this](int completed, int total)
		{
			real_terrain_status_label->setText(tr("Downloading elevation tiles: %1 / %2").arg(completed).arg(total));
		});
		connect(worker, &RealTerrainImportThread::statusMessage, real_terrain_status_label, &QLabel::setText);
		connect(worker, &RealTerrainImportThread::importReady, this, [this](const QByteArray& metres,
			const QByteArray& material_mask_png, const QByteArray& tree_mask_png,
			const QByteArray& road_mask_png, const QByteArray& building_mask_png,
			const QString& feature_status)
		{
			try
			{
				const float terrain_height_scale = (float)terrainHeightScaleDoubleSpinBox->value();
				if(!std::isfinite(terrain_height_scale) || std::fabs(terrain_height_scale) < 1.0e-6f)
					throw glare::Exception("terrain height scale must be non-zero");
				const size_t resolution = REAL_TERRAIN_MAP_RESOLUTION;
				const size_t num_values = resolution * resolution;
				if(metres.size() != (int)(num_values * sizeof(float)))
					throw glare::Exception("elevation map has an invalid size");
				ImageMapFloatRef heightmap = new ImageMapFloat(resolution, resolution, 1);
				std::memcpy(heightmap->getData(), metres.constData(), (size_t)metres.size());
				for(size_t i=0; i<num_values; ++i)
					heightmap->getData()[i] /= terrain_height_scale;

				const std::string temp_path = PlatformUtils::getTempDirPath() + "/metasiberia_real_terrain_" +
					QtUtils::toStdString(QUuid::createUuid().toString(QUuid::WithoutBraces)) + ".exr";
				EXRDecoder::SaveOptions options;
				options.compression_method = EXRDecoder::CompressionMethod_PIZ;
				options.bit_depth = EXRDecoder::BitDepth_32;
				EXRDecoder::saveImageToEXR(*heightmap, temp_path, "", options);
				const URLString map_url = main_window->gui_client.resource_manager->copyLocalFileToResourceDirAndReturnURL(temp_path);
				if(FileUtils::fileExists(temp_path))
					FileUtils::deleteFile(temp_path);
				auto save_mask_resource = [this](const QByteArray& png_data, const QString& name) -> URLString
				{
					const std::string path = PlatformUtils::getTempDirPath() + "/metasiberia_real_terrain_" +
						QtUtils::toStdString(QUuid::createUuid().toString(QUuid::WithoutBraces)) + "_" + QtUtils::toStdString(name) + ".png";
					QFile file(QString::fromStdString(path));
					if(!file.open(QIODevice::WriteOnly) || file.write(png_data) != png_data.size())
						throw glare::Exception("could not write imported terrain mask PNG");
					file.close();
					const URLString url = main_window->gui_client.resource_manager->copyLocalFileToResourceDirAndReturnURL(path);
					if(FileUtils::fileExists(path)) FileUtils::deleteFile(path);
					return url;
				};
				const URLString material_map_url = save_mask_resource(material_mask_png, QStringLiteral("material"));
				const URLString tree_map_url = save_mask_resource(tree_mask_png, QStringLiteral("trees"));
				const URLString road_map_url = save_mask_resource(road_mask_png, QStringLiteral("roads"));
				const URLString building_map_url = save_mask_resource(building_mask_png, QStringLiteral("buildings"));

				const int target_x = real_terrain_section_x_spin_box->value();
				const int target_y = real_terrain_section_y_spin_box->value();
				TerrainSpecSectionWidget* target_section = NULL;
				for(int i=0; i<terrainSectionScrollAreaWidgetContents->layout()->count(); ++i)
				{
					QWidget* child = terrainSectionScrollAreaWidgetContents->layout()->itemAt(i)->widget();
					TerrainSpecSectionWidget* section = dynamic_cast<TerrainSpecSectionWidget*>(child);
					if(section && section->xSpinBox->value() == target_x && section->ySpinBox->value() == target_y)
					{
						target_section = section;
						break;
					}
				}
				if(!target_section)
				{
					newTerrainSectionPushButtonClicked();
					const int last_i = terrainSectionScrollAreaWidgetContents->layout()->count() - 1;
					QWidget* child = last_i >= 0 ? terrainSectionScrollAreaWidgetContents->layout()->itemAt(last_i)->widget() : NULL;
					target_section = dynamic_cast<TerrainSpecSectionWidget*>(child);
				}
				if(!target_section)
					throw glare::Exception("could not create a terrain section for the elevation map");
				target_section->xSpinBox->setValue(target_x);
				target_section->ySpinBox->setValue(target_y);
				target_section->heightmapURLFileSelectWidget->setFilename(QtUtils::toQString(map_url));
				SignalBlocker::setChecked(target_section->heightmapEnabledCheckBox, true);
				target_section->maskMapURLFileSelectWidget->setFilename(QtUtils::toQString(material_map_url));
				target_section->treeMaskMapURLFileSelectWidget->setFilename(QtUtils::toQString(tree_map_url));
				target_section->roadMaskMapURLFileSelectWidget->setFilename(QtUtils::toQString(road_map_url));
				target_section->buildingMaskMapURLFileSelectWidget->setFilename(QtUtils::toQString(building_map_url));
				SignalBlocker::setChecked(target_section->maskMapEnabledCheckBox, true);
				SignalBlocker::setChecked(target_section->treeMaskMapEnabledCheckBox, true);
				SignalBlocker::setChecked(target_section->roadMaskMapEnabledCheckBox, true);
				SignalBlocker::setChecked(target_section->buildingMaskMapEnabledCheckBox, true);
				real_terrain_status_label->setText(tr("Elevation and terrain layers imported into section (%1, %2). %3 Apply world settings to save them.").arg(target_x).arg(target_y).arg(feature_status));
			}
			catch(const std::exception& e)
			{
					real_terrain_status_label->setText(tr("Could not save terrain layers: %1").arg(QString::fromUtf8(e.what())));
			}
			catch(const glare::Exception& e)
			{
				real_terrain_status_label->setText(tr("Could not save terrain layers: %1").arg(QString::fromStdString(e.what())));
			}
		});
		connect(worker, &RealTerrainImportThread::importFailed, this, [this](const QString& message)
		{
			real_terrain_status_label->setText(tr("Elevation import failed: %1").arg(message));
		});
		connect(worker, &QThread::finished, this, [this, worker]()
		{
			real_terrain_import_thread = NULL;
			real_terrain_import_button->setEnabled(real_terrain_group->isEnabled());
			worker->deleteLater();
		});
		worker->start();
	});

	sculpting_status_label = new QLabel(sculpting_tab);
	sculpting_status_label->setWordWrap(true);
	sculpting_status_label->setText(tr("Free camera is enabled with this mode. Left-click the terrain to sculpt."));
	sculpting_layout->addWidget(sculpting_status_label);
	sculpting_layout->addStretch(1);

	settings_tabs->addTab(sculpting_tab, tr("Sculpting"));
	QVBoxLayout* root_layout = new QVBoxLayout(this);
	root_layout->setContentsMargins(0, 0, 0, 0);
	root_layout->addWidget(settings_tabs);

	connect(sculpting_mode_check_box, &QCheckBox::toggled, this, [this](bool enabled)
	{
		setSculptingControlsEnabled(enabled && (!main_window || main_window->connectedToUsersWorldOrGodUser()));
		emit sculptingModeChangedSignal(enabled);
	});
	connect(sculpting_radius_spin_box, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double)
	{
		emit sculptingBrushSettingsChangedSignal((float)sculpting_radius_spin_box->value(), (float)sculpting_strength_spin_box->value(), (float)sculpting_target_height_spin_box->value());
	});
	connect(sculpting_strength_spin_box, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double)
	{
		emit sculptingBrushSettingsChangedSignal((float)sculpting_radius_spin_box->value(), (float)sculpting_strength_spin_box->value(), (float)sculpting_target_height_spin_box->value());
	});
	connect(sculpting_target_height_spin_box, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double)
	{
		emit sculptingBrushSettingsChangedSignal((float)sculpting_radius_spin_box->value(), (float)sculpting_strength_spin_box->value(), (float)sculpting_target_height_spin_box->value());
	});
	auto updateIslandSettings = [this]()
	{
		const int sea_floor = sculpting_island_sea_floor_slider->value();
		const int land_base = sculpting_island_land_base_slider->value();
		const int peak = sculpting_island_peak_slider->value();
		const int seed = sculpting_island_seed_slider->value();
		sculpting_island_sea_floor_value->setText(QString::number(sea_floor) + tr(" m"));
		sculpting_island_land_base_value->setText(QString::number(land_base) + tr(" m"));
		sculpting_island_peak_value->setText(QString::number(peak) + tr(" m"));
		sculpting_island_seed_value->setText(QString::number(seed));
		const int island_kind = myClamp(sculpting_map_target_combo->currentData().toInt() - 8, 0, 23);
		sculpting_island_preview_label->setText(tr("Island preview: %1").arg(sculpting_map_target_combo->currentText()));
		const int image_w = 320, image_h = 112;
		QImage preview(image_w, image_h, QImage::Format_RGB32);
		for(int y=0; y<image_h; ++y)
		for(int x=0; x<image_w; ++x)
		{
			const float u = ((float)x / (float)(image_w - 1) * 2.f - 1.f) * 1.12f;
			const float v = ((float)y / (float)(image_h - 1) * 2.f - 1.f) * 1.12f;
			const float z = TerrainSystem::getIslandPreviewHeight(island_kind, u, v, seed, (float)sea_floor, (float)land_base, (float)peak);
			QColor colour;
			if(z <= sea_floor + 0.25f)
			{
				const int wave = (int)(3.f * std::sin(x * 0.14f + y * 0.04f));
				colour = QColor(22 + wave, 69 + wave, 103 + wave);
			}
			else
			{
				const float height_fraction = myClamp((z - land_base) / (float)myMax(1, peak - land_base), 0.f, 1.f);
				const float left = TerrainSystem::getIslandPreviewHeight(island_kind, u - 0.018f, v, seed, (float)sea_floor, (float)land_base, (float)peak);
				const float above = TerrainSystem::getIslandPreviewHeight(island_kind, u, v - 0.018f, seed, (float)sea_floor, (float)land_base, (float)peak);
				const float light = myClamp(0.88f + (left - z + above - z) * 0.012f, 0.58f, 1.18f);
				QColor base = height_fraction > 0.74f ? QColor(194, 190, 177) : height_fraction > 0.38f ? QColor(83, 119, 66) : QColor(150, 132, 85);
				colour = QColor(myClamp((int)(base.red() * light), 0, 255), myClamp((int)(base.green() * light), 0, 255), myClamp((int)(base.blue() * light), 0, 255));
			}
			preview.setPixelColor(x, y, colour);
		}
		sculpting_island_preview_image->setPixmap(QPixmap::fromImage(preview));
		emit sculptingIslandSettingsChangedSignal((float)sea_floor, (float)land_base, (float)peak, seed);
	};
	updateIslandSettings(); // Initialise displayed values; the GUI client starts with the same defaults.
	connect(sculpting_island_sea_floor_slider, &QSlider::valueChanged, this, [updateIslandSettings](int) { updateIslandSettings(); });
	connect(sculpting_island_land_base_slider, &QSlider::valueChanged, this, [updateIslandSettings](int) { updateIslandSettings(); });
	connect(sculpting_island_peak_slider, &QSlider::valueChanged, this, [updateIslandSettings](int) { updateIslandSettings(); });
	connect(sculpting_island_seed_slider, &QSlider::valueChanged, this, [updateIslandSettings](int) { updateIslandSettings(); });
	connect(sculpting_island_random_seed_button, &QPushButton::clicked, this, [this]()
	{
		sculpting_island_seed_slider->setValue(QRandomGenerator::global()->bounded(1, 10000));
	});
	connect(sculpting_island_regenerate_button, &QPushButton::clicked, this, [this]() { emit sculptingIslandRegenerateRequestedSignal(); });
	connect(sculpting_island_place_button, &QPushButton::clicked, this, [this]()
	{
		emit sculptingIslandPlacementRequestedSignal();
		sculpting_status_label->setText(tr("Island placement is ready. Click the desired location in the world."));
	});
	connect(sculpting_map_target_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, updateIslandSettings](int index)
	{
		const int target = sculpting_map_target_combo->itemData(index).toInt();
		if(target >= 1 && target <= 3)
			sculpting_layers_list->setCurrentRow(target - 1);
		else if(target == 7)
			sculpting_layers_list->setCurrentRow(3);
		else
			sculpting_layers_list->setCurrentRow(-1);
		emit sculptingMapPaintTargetChangedSignal(target);
		const bool island_mode = target >= 8 && target <= 31;
		if(island_mode) updateIslandSettings();
		sculpting_island_settings_group->setVisible(island_mode);
		sculpting_strength_label->setVisible(!island_mode);
		sculpting_strength_spin_box->setVisible(!island_mode);
		sculpting_target_height_label->setVisible(!island_mode);
		sculpting_target_height_spin_box->setVisible(!island_mode);
		const bool height_edit = target == 0;
		const bool undoable = (target >= 0 && target <= 7) || (target >= 8 && target <= 33);
		const bool enabled = sculpting_mode_check_box->isChecked() && (!main_window || main_window->connectedToUsersWorldOrGodUser());
		sculpting_undo_button->setEnabled(enabled && undoable);
		sculpting_redo_button->setEnabled(enabled && undoable);
		if(height_edit)
			sculpting_status_label->setText(tr("Free camera is enabled with this mode. Left-click the terrain to sculpt."));
		else if(island_mode)
		{
			if(sculpting_radius_spin_box->value() < 128.0)
				sculpting_radius_spin_box->setValue(1024.0);
			sculpting_status_label->setText(tr("Review the preview and settings, press Add island to world, then click the desired location. Regenerate repeats the last placed island."));
		}
		else if(target == 1 || target == 2 || target == 7)
			sculpting_status_label->setText(tr("Left-drag to paint the selected surface layer. Assign its texture in the World tab; without a texture, only the layer mask changes."));
		else if(target == 3)
			sculpting_status_label->setText(tr("Left-drag to paint vegetation. This updates the surface material and recalculates vegetation scattering."));
		else if(target == 32)
			sculpting_status_label->setText(tr("Paint procedural grass on the terrain. Trees are blocked in the painted area."));
		else if(target == 33)
			sculpting_status_label->setText(tr("Paint procedural grass and allow trees in the painted area."));
		else if(target == 4 || target == 5)
			sculpting_status_label->setText(tr("Left-drag to allow or block trees. Tree placement changes when the stroke is released."));
		else
			sculpting_status_label->setText(tr("Left-drag to restore the source surface and tree masks in this area."));
	});
	connect(sculpting_layers_list, &QListWidget::currentRowChanged, this, [this](int row)
	{
		if(row < 0 || row >= sculpting_layers_list->count()) return;
		const int target = sculpting_layers_list->item(row)->data(Qt::UserRole).toInt();
		const int combo_index = sculpting_map_target_combo->findData(target);
		if(combo_index >= 0) sculpting_map_target_combo->setCurrentIndex(combo_index);
	});
	connect(sculpting_undo_button, &QPushButton::clicked, this, [this]() { emit sculptingUndoSignal(); });
	connect(sculpting_redo_button, &QPushButton::clicked, this, [this]() { emit sculptingRedoSignal(); });
	connect(sculpting_basic_accordion_button, &QToolButton::toggled, this, [this](bool expanded)
	{
		sculpting_basic_panel->setVisible(expanded);
		sculpting_basic_accordion_button->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
	});
	connect(sculpting_layers_accordion_button, &QToolButton::toggled, this, [this](bool expanded)
	{
		sculpting_layers_group->setVisible(expanded);
		sculpting_layers_accordion_button->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
	});
	connect(sculpting_shape_accordion_button, &QToolButton::toggled, this, [this](bool expanded)
	{
		sculpting_shape_panel->setVisible(expanded);
		sculpting_shape_accordion_button->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
	});
	connect(sculpting_stamp_accordion_button, &QToolButton::toggled, this, [this](bool expanded)
	{
		sculpting_stamp_panel->setVisible(expanded);
		sculpting_stamp_accordion_button->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
	});

	setSculptingControlsEnabled(false);
}


void WorldSettingsWidget::setSculptingControlsEnabled(bool enabled)
{
	if(!sculpting_basic_panel)
		return;
	sculpting_basic_panel->setEnabled(enabled);
	sculpting_shape_panel->setEnabled(enabled);
	sculpting_stamp_panel->setEnabled(enabled);
	sculpting_island_settings_group->setEnabled(enabled);
	sculpting_radius_spin_box->setEnabled(enabled);
	sculpting_strength_spin_box->setEnabled(enabled);
	sculpting_target_height_spin_box->setEnabled(enabled);
	sculpting_map_target_combo->setEnabled(enabled);
	sculpting_layers_group->setEnabled(enabled);
	real_terrain_group->setEnabled(enabled);
	const int target = sculpting_map_target_combo->currentData().toInt();
	const bool undoable = (target >= 0 && target <= 7) || (target >= 8 && target <= 33);
	sculpting_undo_button->setEnabled(enabled && undoable);
	sculpting_redo_button->setEnabled(enabled && undoable);
	sculpting_status_label->setEnabled(enabled);
}


void WorldSettingsWidget::retranslateSculptingTab()
{
	if(!settings_tabs)
		return;
	settings_tabs->setTabText(0, tr("World"));
	settings_tabs->setTabText(1, tr("Sculpting"));
	sculpting_mode_check_box->setText(tr("Enable sculpting mode"));
	sculpting_undo_button->setText(tr("Undo"));
	sculpting_redo_button->setText(tr("Redo"));
	sculpting_radius_label->setText(tr("Brush radius"));
	sculpting_strength_label->setText(tr("Strength"));
	sculpting_target_height_label->setText(tr("Target height"));
	sculpting_layers_group->setTitle(tr("Surface layers"));
	sculpting_layers_accordion_button->setText(tr("Surface layers"));
	const QString material_layer_names[4] = { tr("Rock"), tr("Ground / sand"), tr("Vegetation"), tr("Overlay material") };
	for(int i=0; i<4; ++i)
	{
		QListWidgetItem* item = sculpting_layers_list->item(i);
		if(item) item->setText(material_layer_names[i]);
	}
	sculpting_island_settings_group->setTitle(tr("Island settings"));
	sculpting_island_preview_label->setText(tr("Island preview: %1").arg(sculpting_map_target_combo->currentText()));
	sculpting_island_sea_floor_label->setText(tr("Sea floor"));
	sculpting_island_land_base_label->setText(tr("Land base"));
	sculpting_island_peak_label->setText(tr("Maximum peak"));
	sculpting_island_seed_label->setText(tr("Seed"));
	sculpting_island_random_seed_button->setText(tr("Randomize seed"));
	sculpting_island_regenerate_button->setText(tr("Regenerate island"));
	sculpting_island_place_button->setText(tr("Add island to world"));
	sculpting_island_sea_floor_value->setText(QString::number(sculpting_island_sea_floor_slider->value()) + tr(" m"));
	sculpting_island_land_base_value->setText(QString::number(sculpting_island_land_base_slider->value()) + tr(" m"));
	sculpting_island_peak_value->setText(QString::number(sculpting_island_peak_slider->value()) + tr(" m"));
	sculpting_island_seed_value->setText(QString::number(sculpting_island_seed_slider->value()));
	sculpting_map_target_label->setText(tr("Edit / generate"));
	real_terrain_group->setTitle(tr("Real-world terrain"));
	real_terrain_map_label->setText(tr("Select an area on the map. Drag with the left mouse button; right-drag pans, wheel zooms."));
	real_terrain_basemap_combo->setItemText(0, tr("Metasiberia Map"));
	real_terrain_basemap_combo->setItemText(1, tr("Topographic map"));
	real_terrain_basemap_combo->setItemText(2, tr("Satellite imagery"));
	const int basemap = real_terrain_basemap_combo->currentIndex();
	real_terrain_attribution_label->setText(basemap == 2 ? tr("Imagery © Esri") : basemap == 1 ? tr("Map data: © OpenStreetMap contributors, SRTM | Map style: OpenTopoMap") : tr("© Metasiberia Map | Map data: © OpenStreetMap contributors"));
	real_terrain_rectangle_button->setText(tr("Rectangle"));
	real_terrain_freehand_button->setText(tr("Freehand contour"));
	real_terrain_clear_button->setText(tr("Clear selection"));
	real_terrain_zoom_out_button->setText(tr("−"));
	real_terrain_zoom_out_button->setToolTip(tr("Zoom out"));
	real_terrain_zoom_in_button->setText(tr("+"));
	real_terrain_zoom_in_button->setToolTip(tr("Zoom in"));
	real_terrain_section_x_label->setText(tr("Target section X"));
	real_terrain_section_y_label->setText(tr("Target section Y"));
	real_terrain_import_button->setText(tr("Import terrain and layers"));
	real_terrain_source_label->setText(tr("Imports height, material, tree, road and building maps into the chosen terrain section. OSM road/building detail is available for areas up to about 25 km across."));
	const QString map_target_names[8] = {
		tr("Terrain height"), tr("Rock material"), tr("Ground / sand material"), tr("Vegetation material"),
		tr("Allow trees"), tr("Block trees"), tr("Restore source map"), tr("Overlay material (e.g. beach / snow)")
	};
	for(int i=0; i<8; ++i)
		sculpting_map_target_combo->setItemText(i, map_target_names[i]);
	const int allow_trees_item = sculpting_map_target_combo->findData(4);
	const int block_trees_item = sculpting_map_target_combo->findData(5);
	const int grass_item = sculpting_map_target_combo->findData(32);
	const int trees_grass_item = sculpting_map_target_combo->findData(33);
	if(allow_trees_item >= 0) sculpting_map_target_combo->setItemText(allow_trees_item, tr("Tree mask: allow"));
	if(block_trees_item >= 0) sculpting_map_target_combo->setItemText(block_trees_item, tr("Tree mask: block"));
	if(grass_item >= 0) sculpting_map_target_combo->setItemText(grass_item, tr("Paint grass"));
	if(trees_grass_item >= 0) sculpting_map_target_combo->setItemText(trees_grass_item, tr("Paint trees and grass"));
	const QString island_names[24] = {
		tr("Island: classic volcanic"), tr("Island: high mountainous"), tr("Island: low coral cay"), tr("Island: barrier / long"),
		tr("Island: twin linked"), tr("Island: caldera"), tr("Island: table / mesa"), tr("Island: rias / drowned"),
		tr("Island: spit / sandbar"), tr("Island: arch rocks"), tr("Island: volcanic chain"), tr("Island: reef atoll"),
		tr("Island: ring"), tr("Island: crescent"), tr("Island: star"), tr("Island: spiral"), tr("Island: serpent"),
		tr("Island: shattered"), tr("Island: crystal spires"), tr("Island: cliff plateau"), tr("Island: mushroom cap"),
		tr("Island: heart"), tr("Island: sky needle"), tr("Island: lagoon chain")
	};
	for(int i=0; i<24; ++i)
	{
		const int item = sculpting_map_target_combo->findData(8 + i);
		if(item >= 0) sculpting_map_target_combo->setItemText(item, island_names[i]);
	}
	const int real_island_item = sculpting_map_target_combo->findData(8);
	const int fantasy_island_item = sculpting_map_target_combo->findData(20);
	if(real_island_item > 0) sculpting_map_target_combo->setItemText(real_island_item - 1, tr("Real islands"));
	if(fantasy_island_item > 0) sculpting_map_target_combo->setItemText(fantasy_island_item - 1, tr("Fantasy islands"));
	sculpting_radius_spin_box->setSuffix(tr(" m"));
	sculpting_strength_spin_box->setSuffix(tr(" m"));
	sculpting_target_height_spin_box->setSuffix(tr(" m"));
	sculpting_basic_accordion_button->setText(tr("Basic tools"));
	sculpting_shape_accordion_button->setText(tr("Shape tools"));
	sculpting_stamp_accordion_button->setText(tr("Stamps"));
	const int map_target = sculpting_map_target_combo->currentData().toInt();
	if(map_target == 0)
		sculpting_status_label->setText(tr("Free camera is enabled with this mode. Left-click the terrain to sculpt."));
	else if(map_target >= 8 && map_target <= 31)
		sculpting_status_label->setText(tr("Choose an island type, set sea floor, land base, peak and seed, then click terrain. Regenerate repeats the island at the last placement."));
	else if(map_target == 32)
		sculpting_status_label->setText(tr("Paint procedural grass on the terrain. Trees are blocked in the painted area."));
	else if(map_target == 33)
		sculpting_status_label->setText(tr("Paint procedural grass and allow trees in the painted area."));
	else
		sculpting_status_label->setText(tr("Free camera is enabled with this mode. Left-click to paint the selected map; strength controls opacity."));
	const QString tool_names[17] = {
		tr("Ridge"), tr("Raise"), tr("Lower"), tr("Soft raise"), tr("Hard raise"),
		tr("Soft lower"), tr("Hard lower"), tr("Pinch"), tr("Inflate"), tr("Deflate"),
		tr("Clay build"), tr("Blob"), tr("Amplify"), tr("Dampen"), tr("Smooth"),
		tr("Polish"), tr("Sharpen")
	};
	for(int i=0; i<17; ++i)
	{
		sculpting_tool_buttons[i]->setText(tool_names[i]);
		sculpting_tool_buttons[i]->setToolTip(tr("%1: select the brush, then left-drag on the terrain. The thumbnail shows the approximate height profile; radius and strength are set above.").arg(tool_names[i]));
	}
	const QString shape_tool_names[21] = {
		tr("Flatten"), tr("Plateau"), tr("Ramp"), tr("Cliff"), tr("Wall ridge"),
		tr("Basin"), tr("Gorge"), tr("Berm"), tr("Saddle"), tr("Notch"),
		tr("Lake"), tr("Fill level"), tr("Lowland"), tr("Coast / beach"), tr("Shelf"),
		tr("Cove"), tr("Spit"), tr("River / valley"), tr("Channel"), tr("Delta"), tr("Carve sea")
	};
	for(int i=0; i<21; ++i)
	{
		sculpting_shape_tool_buttons[i]->setText(shape_tool_names[i]);
		sculpting_shape_tool_buttons[i]->setToolTip(tr("%1: select the shape, set the target height if needed, then left-drag on the terrain. The thumbnail shows its approximate profile.").arg(shape_tool_names[i]));
	}
	const QString stamp_tool_names[16] = {
		tr("Volcano"), tr("Crater"), tr("Hill"), tr("Cone"), tr("Mesa"), tr("Caldera"),
		tr("Ridge stamp"), tr("Spire"), tr("Pyramid"), tr("Bowl"), tr("Arch"), tr("Atoll"),
		tr("Twin hills"), tr("Dunes"), tr("Tor rocks"), tr("Ramp stamp")
	};
	for(int i=0; i<16; ++i)
	{
		sculpting_stamp_tool_buttons[i]->setText(stamp_tool_names[i]);
		sculpting_stamp_tool_buttons[i]->setToolTip(tr("%1: select the stamp, then click or drag to apply its height shape. Brush radius controls its size; the thumbnail is schematic.").arg(stamp_tool_names[i]));
	}
	sculpting_radius_spin_box->setToolTip(tr("Sets the brush radius in metres. Use a smaller radius for detail and a larger radius for broad changes."));
	sculpting_strength_spin_box->setToolTip(tr("Controls height change in metres. For surface and tree masks it also controls paint opacity: roughly 10% per strength unit."));
	sculpting_target_height_spin_box->setToolTip(tr("Target elevation used by height tools such as Flatten and Fill to level."));
	sculpting_map_target_label->setToolTip(tr("Choose whether strokes sculpt height, paint a surface or vegetation mask, or place an island."));
	sculpting_layers_accordion_button->setToolTip(tr("Expand or collapse the paintable terrain material layers."));
	const QString target_tooltips[8] = {
		tr("Sculpt terrain height; click or drag across the terrain."),
		tr("Paint the rock surface weight. Assign a rock texture in World settings to see the material."),
		tr("Paint the ground and sand surface weight. Assign a texture in World settings to see the material."),
		tr("Paint the vegetation surface weight. This mask also controls terrain vegetation scattering."),
		tr("Paint the tree placement mask to allow trees in this area."),
		tr("Paint the tree placement mask to block trees in this area."),
		tr("Restore the imported surface and tree masks in the brushed area."),
		tr("Paint the independent overlay texture, such as beach, snow, or rock.")
	};
	for(int i=0; i<8; ++i)
	{
		const int item = sculpting_map_target_combo->findData(i);
		if(item >= 0) sculpting_map_target_combo->setItemData(item, target_tooltips[i], Qt::ToolTipRole);
	}
	const int grass_tooltip_item = sculpting_map_target_combo->findData(32);
	const int trees_grass_tooltip_item = sculpting_map_target_combo->findData(33);
	if(grass_tooltip_item >= 0) sculpting_map_target_combo->setItemData(grass_tooltip_item, tr("Paint procedural grass by adding vegetation. Trees are blocked in the painted area."), Qt::ToolTipRole);
	if(trees_grass_tooltip_item >= 0) sculpting_map_target_combo->setItemData(trees_grass_tooltip_item, tr("Paint procedural grass and allow trees in the painted area."), Qt::ToolTipRole);
	for(int i=0; i<24; ++i)
	{
		const int item = sculpting_map_target_combo->findData(8 + i);
		if(item >= 0) sculpting_map_target_combo->setItemData(item, tr("Preview and place this island shape. Click Add island to world, then click the location in the scene."), Qt::ToolTipRole);
	}
	for(int i=0; i<4; ++i)
	{
		QListWidgetItem* item = sculpting_layers_list->item(i);
		if(item)
		{
			const QString base = i == 0 ? tr("Paint the rock weight map. The assigned rock texture appears where this layer has weight.") :
				i == 1 ? tr("Paint the ground / sand weight map. The assigned ground texture appears where this layer has weight.") :
				i == 2 ? tr("Paint the vegetation weight map. Tree placement also depends on the separate tree mask.") :
				tr("Paint an independent overlay, such as beach or snow. This layer blends over the three base materials.");
			item->setToolTip(base + tr(" Surface texture is assigned in the World tab. Select this row, then paint in the scene."));
		}
	}
	waterCausticsEnabledCheckBox->setToolTip(tr("Show or hide projected underwater caustics on terrain below the water level. This does not turn the water itself off."));
}


WorldSettingsWidget::~WorldSettingsWidget()
{
	if(real_terrain_import_thread)
	{
		real_terrain_import_thread->cancelImport();
		real_terrain_import_thread->wait();
		delete real_terrain_import_thread;
		real_terrain_import_thread = NULL;
	}
}


void WorldSettingsWidget::init(MainWindow* main_window_)
{
	main_window = main_window_;

	updateControlsEditable();
}


void WorldSettingsWidget::retranslateUiText()
{
	retranslateUi(this);
	for(int i=0; i<4; ++i)
	{
		if(QPushButton* button = findChild<QPushButton*>(QString("terrainMaterialBrowserButton%1").arg(i)))
		{
			button->setText(tr("Choose material"));
			button->setToolTip(tr("Open the material browser and assign its colour texture to this terrain layer."));
		}
		if(QPushButton* button = findChild<QPushButton*>(QString("terrainPolyHavenButton%1").arg(i)))
		{
			button->setText(tr("Poly Haven"));
			button->setToolTip(tr("Search Poly Haven textures, preview the albedo, and assign it to this layer."));
		}
		if(terrain_material_preview_labels[i])
			terrain_material_preview_labels[i]->setToolTip(tr("Preview of the terrain material texture"));
		updateTerrainMaterialPreview(i);
	}
	detailColMapURLs0FileSelectWidget->setToolTip(tr("Base-colour texture for the rock material layer. Paint the red channel of the terrain material mask to apply it."));
	detailColMapURLs1FileSelectWidget->setToolTip(tr("Base-colour texture for the ground / sand material layer. Paint the green channel of the terrain material mask to apply it."));
	detailColMapURLs2FileSelectWidget->setToolTip(tr("Base-colour texture for the vegetation material layer. Paint the blue channel of the terrain material mask to apply it."));
	detailColMapURLs3FileSelectWidget->setToolTip(tr("Base-colour texture for the independent overlay layer. Paint the Overlay material layer to apply it over rock, ground and vegetation."));
	retranslateSculptingTab();

	QLayout* sections_layout = terrainSectionScrollAreaWidgetContents->layout();
	if(!sections_layout)
		return;

	for(int i=0; i<sections_layout->count(); ++i)
	{
		if(QWidget* widget = sections_layout->itemAt(i)->widget())
		{
			TerrainSpecSectionWidget* section_widget = dynamic_cast<TerrainSpecSectionWidget*>(widget);
			if(section_widget)
				section_widget->retranslateUi(section_widget);
		}
	}
}


bool WorldSettingsWidget::shouldStartTerrainSectionResize(const QPoint& pos_in_scroll_area) const
{
	const int resize_hot_zone_px = 6;
	const int h = terrainSectionScrollArea->height();
	return (pos_in_scroll_area.y() >= h - resize_hot_zone_px) && (pos_in_scroll_area.y() <= h + resize_hot_zone_px);
}


void WorldSettingsWidget::setTerrainSectionAreaHeight(int target_height)
{
	const int min_height = 70;
	const int max_height = std::max(min_height, this->height() - 180);
	const int clamped_height = std::max(min_height, std::min(target_height, max_height));

	terrainSectionScrollArea->setMinimumHeight(clamped_height);
	terrainSectionScrollArea->setMaximumHeight(clamped_height);
}


bool WorldSettingsWidget::eventFilter(QObject* watched, QEvent* event)
{
	const bool relevant_object = (watched == terrainSectionScrollArea) || (watched == terrainSectionScrollArea->viewport());
	if(!relevant_object)
		return QWidget::eventFilter(watched, event);

	auto toScrollAreaPos = [&](const QPoint& pos_in_watched) -> QPoint
	{
		if(watched == terrainSectionScrollArea)
			return pos_in_watched;
		return terrainSectionScrollArea->viewport()->mapTo(terrainSectionScrollArea, pos_in_watched);
	};

	if(event->type() == QEvent::MouseButtonPress)
	{
		QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
		if(mouse_event->button() == Qt::LeftButton)
		{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
			const QPoint local_pos = mouse_event->position().toPoint();
			const int global_y = mouse_event->globalPosition().toPoint().y();
#else
			const QPoint local_pos = mouse_event->pos();
			const int global_y = mouse_event->globalY();
#endif
			if(shouldStartTerrainSectionResize(toScrollAreaPos(local_pos)))
			{
				terrain_section_resize_drag_active = true;
				terrain_section_resize_drag_start_global_y = global_y;
				terrain_section_resize_drag_start_height = terrainSectionScrollArea->height();
				terrainSectionScrollArea->setCursor(Qt::SizeVerCursor);
				return true;
			}
		}
	}
	else if(event->type() == QEvent::MouseMove)
	{
		QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
		const QPoint local_pos = mouse_event->position().toPoint();
		const int global_y = mouse_event->globalPosition().toPoint().y();
#else
		const QPoint local_pos = mouse_event->pos();
		const int global_y = mouse_event->globalY();
#endif

		if(terrain_section_resize_drag_active)
		{
			const int delta_y = global_y - terrain_section_resize_drag_start_global_y;
			setTerrainSectionAreaHeight(terrain_section_resize_drag_start_height + delta_y);
			return true;
		}

		if(shouldStartTerrainSectionResize(toScrollAreaPos(local_pos)))
			terrainSectionScrollArea->setCursor(Qt::SizeVerCursor);
		else
			terrainSectionScrollArea->unsetCursor();
	}
	else if(event->type() == QEvent::MouseButtonRelease)
	{
		QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
		if(terrain_section_resize_drag_active && (mouse_event->button() == Qt::LeftButton))
		{
			terrain_section_resize_drag_active = false;
			terrainSectionScrollArea->unsetCursor();
			return true;
		}
	}
	else if(event->type() == QEvent::Leave)
	{
		if(!terrain_section_resize_drag_active)
			terrainSectionScrollArea->unsetCursor();
	}

	return QWidget::eventFilter(watched, event);
}


void WorldSettingsWidget::setFromWorldSettings(const WorldSettings& world_settings)
{
	QSignalBlocker blocker(this);

	QtUtils::ClearLayout(terrainSectionScrollAreaWidgetContents->layout(), /*delete widgets=*/true);

	for(size_t i=0; i<world_settings.terrain_spec.section_specs.size(); ++i)
	{
		const TerrainSpecSection& section_spec = world_settings.terrain_spec.section_specs[i];

		TerrainSpecSectionWidget* new_section_widget = new TerrainSpecSectionWidget(this);
		new_section_widget->xSpinBox->setValue(section_spec.x);
		new_section_widget->ySpinBox->setValue(section_spec.y);
		new_section_widget->heightmapURLFileSelectWidget->setFilename(QtUtils::toQString(section_spec.heightmap_URL));
		new_section_widget->maskMapURLFileSelectWidget->setFilename(QtUtils::toQString(section_spec.mask_map_URL));
		new_section_widget->treeMaskMapURLFileSelectWidget->setFilename(QtUtils::toQString(section_spec.tree_mask_map_URL));
		new_section_widget->roadMaskMapURLFileSelectWidget->setFilename(QtUtils::toQString(section_spec.road_mask_map_URL));
		new_section_widget->buildingMaskMapURLFileSelectWidget->setFilename(QtUtils::toQString(section_spec.building_mask_map_URL));
		SignalBlocker::setChecked(new_section_widget->heightmapEnabledCheckBox, !BitUtils::isBitSet(section_spec.disabled_map_flags, TerrainSpecSection::HEIGHTMAP_DISABLED_FLAG));
		SignalBlocker::setChecked(new_section_widget->maskMapEnabledCheckBox, !BitUtils::isBitSet(section_spec.disabled_map_flags, TerrainSpecSection::MASK_MAP_DISABLED_FLAG));
		SignalBlocker::setChecked(new_section_widget->treeMaskMapEnabledCheckBox, !BitUtils::isBitSet(section_spec.disabled_map_flags, TerrainSpecSection::TREE_MASK_MAP_DISABLED_FLAG));
		SignalBlocker::setChecked(new_section_widget->roadMaskMapEnabledCheckBox, !BitUtils::isBitSet(section_spec.disabled_map_flags, TerrainSpecSection::ROAD_MASK_MAP_DISABLED_FLAG));
		SignalBlocker::setChecked(new_section_widget->buildingMaskMapEnabledCheckBox, !BitUtils::isBitSet(section_spec.disabled_map_flags, TerrainSpecSection::BUILDING_MASK_MAP_DISABLED_FLAG));

		const bool editable = main_window->connectedToUsersWorldOrGodUser();
		new_section_widget->updateControlsEditable(editable);

		terrainSectionScrollAreaWidgetContents->layout()->addWidget(new_section_widget);
		connect(new_section_widget, SIGNAL(removeButtonClickedSignal()), this, SLOT(removeTerrainSectionButtonClickedSlot()));
		connect(new_section_widget->heightmapEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
		connect(new_section_widget->maskMapEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
		connect(new_section_widget->treeMaskMapEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
		connect(new_section_widget->roadMaskMapEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
		connect(new_section_widget->buildingMaskMapEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	}

	detailColMapURLs0FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[0]));
	detailColMapURLs1FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[1]));
	detailColMapURLs2FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[2]));
	detailColMapURLs3FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[3]));
	for(int i=0; i<4; ++i)
		updateTerrainMaterialPreview(i);
	SignalBlocker::setChecked(detailColMapURLs0EnabledCheckBox, !BitUtils::isBitSet(world_settings.terrain_spec.disabled_detail_map_flags, TerrainSpec::DETAIL_COL_MAP_0_DISABLED_FLAG));
	SignalBlocker::setChecked(detailColMapURLs1EnabledCheckBox, !BitUtils::isBitSet(world_settings.terrain_spec.disabled_detail_map_flags, TerrainSpec::DETAIL_COL_MAP_1_DISABLED_FLAG));
	SignalBlocker::setChecked(detailColMapURLs2EnabledCheckBox, !BitUtils::isBitSet(world_settings.terrain_spec.disabled_detail_map_flags, TerrainSpec::DETAIL_COL_MAP_2_DISABLED_FLAG));
	SignalBlocker::setChecked(detailColMapURLs3EnabledCheckBox, !BitUtils::isBitSet(world_settings.terrain_spec.disabled_detail_map_flags, TerrainSpec::DETAIL_COL_MAP_3_DISABLED_FLAG));

	detailHeightMapURLs0FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_height_map_URLs[0]));
	SignalBlocker::setChecked(detailHeightMapURLs0EnabledCheckBox, !BitUtils::isBitSet(world_settings.terrain_spec.disabled_detail_map_flags, TerrainSpec::DETAIL_HEIGHT_MAP_0_DISABLED_FLAG));

	terrainSectionWidthDoubleSpinBox->setValue(world_settings.terrain_spec.terrain_section_width_m);
	terrainHeightScaleDoubleSpinBox->setValue(world_settings.terrain_spec.terrain_height_scale);
	defaultTerrainZDoubleSpinBox->setValue(world_settings.terrain_spec.default_terrain_z);
	waterZDoubleSpinBox->setValue(world_settings.terrain_spec.water_z);
	waterCheckBox->setChecked(BitUtils::isBitSet(world_settings.terrain_spec.flags, TerrainSpec::WATER_ENABLED_FLAG));
	exactHeightmapCheckBox->setChecked(BitUtils::isBitSet(world_settings.terrain_spec.flags, TerrainSpec::EXACT_HEIGHTMAP_FLAG));

	SignalBlocker::setValue(this->sunThetaSettingRealControl, ::radToDegree(world_settings.sun_theta));
	SignalBlocker::setValue(this->sunPhiSettingRealControl,   ::radToDegree(world_settings.sun_phi));
	SignalBlocker::setValue(layer0ASpinBox,           world_settings.fog_settings.layer_0_A);
	SignalBlocker::setValue(layer0HeightScaleSpinBox, world_settings.fog_settings.layer_0_scale_height);
	SignalBlocker::setValue(layer1ASpinBox,           world_settings.fog_settings.layer_1_A);
	SignalBlocker::setValue(layer1HeightScaleSpinBox, world_settings.fog_settings.layer_1_scale_height);
	SignalBlocker::setChecked(volumetricCloudsEnabledCheckBox, world_settings.volumetric_cloud_settings.enabled);
	SignalBlocker::setValue(cloudBottomZWorldRealControl, world_settings.volumetric_cloud_settings.bottom_z);
	SignalBlocker::setValue(cloudTopZWorldRealControl, world_settings.volumetric_cloud_settings.top_z);
	SignalBlocker::setValue(cloudCoverageWorldRealControl, world_settings.volumetric_cloud_settings.coverage);
	SignalBlocker::setValue(cloudDensityWorldRealControl, world_settings.volumetric_cloud_settings.density);
	SignalBlocker::setValue(cloudWindSpeedWorldRealControl, world_settings.volumetric_cloud_settings.wind_speed);
	SignalBlocker::setValue(cloudEdgeSoftnessWorldRealControl, world_settings.volumetric_cloud_settings.edge_softness);
	SignalBlocker::setValue(cloudHorizonFadeWorldRealControl, world_settings.volumetric_cloud_settings.horizon_fade);
	SignalBlocker::setValue(cloudShapePeriodWorldRealControl, world_settings.volumetric_cloud_settings.shape_period);
	SignalBlocker::setValue(cloudDetailPeriodWorldRealControl, world_settings.volumetric_cloud_settings.detail_period);
	SignalBlocker::setValue(cloudMaxMarchDistWorldRealControl, world_settings.volumetric_cloud_settings.max_march_dist);
	SignalBlocker::setValue(cloudWindDirectionWorldRealControl, world_settings.volumetric_cloud_settings.wind_direction_deg);
	SignalBlocker::setValue(cloudDirectSunWorldRealControl, world_settings.cloud_lighting_settings.direct_sun_strength);
	SignalBlocker::setValue(cloudSkyLightWorldRealControl, world_settings.cloud_lighting_settings.sky_light_strength);
	SignalBlocker::setValue(cloudSunsetResponseWorldRealControl, world_settings.cloud_lighting_settings.sunset_response);
	SignalBlocker::setValue(cloudGroundContributionWorldRealControl, world_settings.cloud_lighting_settings.ground_contribution);
	SignalBlocker::setValue(cloudBottomDarknessWorldRealControl, world_settings.cloud_lighting_settings.underside_darkness);
	SignalBlocker::setValue(cloudPhaseGWorldRealControl, world_settings.cloud_lighting_settings.phase_g);
	SignalBlocker::setValue(cloudPhaseBlendWorldRealControl, world_settings.cloud_lighting_settings.phase_blend);
	SignalBlocker::setValue(cloudMultiScatteringWorldRealControl, world_settings.cloud_lighting_settings.multi_scattering);
	SignalBlocker::setValue(cloudScatteringWorldRealControl, world_settings.cloud_lighting_settings.scattering_scale);
	SignalBlocker::setChecked(waterCloudReflectionEnabledCheckBox, world_settings.water_reflection_settings.cloud_reflection_enabled);
	SignalBlocker::setValue(cloudWaterReflectionWorldRealControl, world_settings.water_reflection_settings.cloud_reflection_strength);
	SignalBlocker::setValue(waterCloudReflectionSamplesWorldRealControl, world_settings.water_reflection_settings.cloud_reflection_samples);
	SignalBlocker::setValue(waterCloudReflectionFadeWorldRealControl, world_settings.water_reflection_settings.cloud_reflection_fade);
	SignalBlocker::setValue(waterWaveAmplitudeWorldRealControl, world_settings.water_surface_settings.wave_amplitude);
	SignalBlocker::setValue(waterWaveLengthWorldRealControl, world_settings.water_surface_settings.wave_length);
	SignalBlocker::setValue(waterWaveSteepnessWorldRealControl, world_settings.water_surface_settings.wave_steepness);
	SignalBlocker::setValue(waterWaveSpeedWorldRealControl, world_settings.water_surface_settings.wave_speed);
	SignalBlocker::setValue(waterWaveDirectionWorldRealControl, world_settings.water_surface_settings.wave_direction_deg);
	SignalBlocker::setValue(waterWaveSpreadWorldRealControl, world_settings.water_surface_settings.wave_direction_spread_deg);
	SignalBlocker::setValue(waterSecondaryWaveScaleWorldRealControl, world_settings.water_surface_settings.secondary_wave_scale);
	SignalBlocker::setChecked(waterSurfEnabledCheckBox, world_settings.water_surface_settings.surf_enabled);
	SignalBlocker::setChecked(waterCausticsEnabledCheckBox, world_settings.water_surface_settings.underwater_caustics_enabled);
	SignalBlocker::setValue(waterSurfStrengthWorldRealControl, world_settings.water_surface_settings.surf_strength);
	SignalBlocker::setValue(waterShorelineWidthWorldRealControl, world_settings.water_surface_settings.shoreline_width);
	SignalBlocker::setValue(waterFoamScaleWorldRealControl, world_settings.water_surface_settings.foam_scale);
	SignalBlocker::setValue(waterFoamSpeedWorldRealControl, world_settings.water_surface_settings.foam_speed);
	SignalBlocker::setValue(waterFoamFadeWorldRealControl, world_settings.water_surface_settings.foam_fade);
}


URLString WorldSettingsWidget::getURLForFileSelectWidget(FileSelectWidget* widget)
{
	std::string current_URL_or_path = QtUtils::toStdString(widget->filename());

	// Copy all dependencies into resource directory if they are not there already.
	if(FileUtils::fileExists(current_URL_or_path)) // If this was a local path:
	{
		const std::string local_path = current_URL_or_path;
		const URLString URL = ResourceManager::URLForPathAndHash(local_path, FileChecksum::fileChecksum(local_path));

		// Copy model to local resources dir.
		main_window->gui_client.resource_manager->copyLocalFileToResourceDir(local_path, URL);

		return URL;
	}
	else
	{
		return toURLString(current_URL_or_path);
	}
}


void WorldSettingsWidget::toWorldSettings(WorldSettings& world_settings_out)
{
	world_settings_out.terrain_spec.section_specs.resize(0);
	for(int i=0; i<terrainSectionScrollAreaWidgetContents->layout()->count(); ++i)
	{
		QWidget* widget = terrainSectionScrollAreaWidgetContents->layout()->itemAt(i)->widget();
		TerrainSpecSectionWidget* section_widget = dynamic_cast<TerrainSpecSectionWidget*>(widget);
		if(section_widget)
		{
			TerrainSpecSection section;
			section.x = section_widget->xSpinBox->value();
			section.y = section_widget->ySpinBox->value();
			section.heightmap_URL = getURLForFileSelectWidget(section_widget->heightmapURLFileSelectWidget);
			section.mask_map_URL = getURLForFileSelectWidget(section_widget->maskMapURLFileSelectWidget);
			section.tree_mask_map_URL = getURLForFileSelectWidget(section_widget->treeMaskMapURLFileSelectWidget);
			section.road_mask_map_URL = getURLForFileSelectWidget(section_widget->roadMaskMapURLFileSelectWidget);
			section.building_mask_map_URL = getURLForFileSelectWidget(section_widget->buildingMaskMapURLFileSelectWidget);
			section.disabled_map_flags =
				(section_widget->heightmapEnabledCheckBox->isChecked() ? 0 : TerrainSpecSection::HEIGHTMAP_DISABLED_FLAG) |
				(section_widget->maskMapEnabledCheckBox->isChecked() ? 0 : TerrainSpecSection::MASK_MAP_DISABLED_FLAG) |
				(section_widget->treeMaskMapEnabledCheckBox->isChecked() ? 0 : TerrainSpecSection::TREE_MASK_MAP_DISABLED_FLAG) |
				(section_widget->roadMaskMapEnabledCheckBox->isChecked() ? 0 : TerrainSpecSection::ROAD_MASK_MAP_DISABLED_FLAG) |
				(section_widget->buildingMaskMapEnabledCheckBox->isChecked() ? 0 : TerrainSpecSection::BUILDING_MASK_MAP_DISABLED_FLAG);

			world_settings_out.terrain_spec.section_specs.push_back(section);
		}
	}

	world_settings_out.terrain_spec.detail_col_map_URLs[0] = getURLForFileSelectWidget(detailColMapURLs0FileSelectWidget);
	world_settings_out.terrain_spec.detail_col_map_URLs[1] = getURLForFileSelectWidget(detailColMapURLs1FileSelectWidget);
	world_settings_out.terrain_spec.detail_col_map_URLs[2] = getURLForFileSelectWidget(detailColMapURLs2FileSelectWidget);
	world_settings_out.terrain_spec.detail_col_map_URLs[3] = getURLForFileSelectWidget(detailColMapURLs3FileSelectWidget);
	world_settings_out.terrain_spec.disabled_detail_map_flags =
		(detailColMapURLs0EnabledCheckBox->isChecked() ? 0 : TerrainSpec::DETAIL_COL_MAP_0_DISABLED_FLAG) |
		(detailColMapURLs1EnabledCheckBox->isChecked() ? 0 : TerrainSpec::DETAIL_COL_MAP_1_DISABLED_FLAG) |
		(detailColMapURLs2EnabledCheckBox->isChecked() ? 0 : TerrainSpec::DETAIL_COL_MAP_2_DISABLED_FLAG) |
		(detailColMapURLs3EnabledCheckBox->isChecked() ? 0 : TerrainSpec::DETAIL_COL_MAP_3_DISABLED_FLAG) |
		(detailHeightMapURLs0EnabledCheckBox->isChecked() ? 0 : TerrainSpec::DETAIL_HEIGHT_MAP_0_DISABLED_FLAG);

	world_settings_out.terrain_spec.detail_height_map_URLs[0] = getURLForFileSelectWidget(detailHeightMapURLs0FileSelectWidget);

	world_settings_out.terrain_spec.terrain_section_width_m = (float)terrainSectionWidthDoubleSpinBox->value();
	world_settings_out.terrain_spec.terrain_height_scale = (float)terrainHeightScaleDoubleSpinBox->value();
	world_settings_out.terrain_spec.default_terrain_z = (float)defaultTerrainZDoubleSpinBox->value();
	world_settings_out.terrain_spec.water_z = (float)waterZDoubleSpinBox->value();
	world_settings_out.terrain_spec.flags =
		(waterCheckBox->isChecked() ? TerrainSpec::WATER_ENABLED_FLAG : 0) |
		(exactHeightmapCheckBox->isChecked() ? TerrainSpec::EXACT_HEIGHTMAP_FLAG : 0);

	world_settings_out.sun_theta = ::degreeToRad(this->sunThetaSettingRealControl->value());
	world_settings_out.sun_phi   = ::degreeToRad(this->sunPhiSettingRealControl  ->value());
	world_settings_out.fog_settings.layer_0_A            = (float)layer0ASpinBox->value();
	world_settings_out.fog_settings.layer_0_scale_height = (float)layer0HeightScaleSpinBox->value();
	world_settings_out.fog_settings.layer_1_A            = (float)layer1ASpinBox->value();
	world_settings_out.fog_settings.layer_1_scale_height = (float)layer1HeightScaleSpinBox->value();
	world_settings_out.volumetric_cloud_settings.enabled = volumetricCloudsEnabledCheckBox->isChecked();
	world_settings_out.volumetric_cloud_settings.bottom_z = (float)cloudBottomZWorldRealControl->value();
	world_settings_out.volumetric_cloud_settings.top_z = (float)cloudTopZWorldRealControl->value();
	world_settings_out.volumetric_cloud_settings.coverage = (float)cloudCoverageWorldRealControl->value();
	world_settings_out.volumetric_cloud_settings.density = (float)cloudDensityWorldRealControl->value();
	world_settings_out.volumetric_cloud_settings.wind_speed = (float)cloudWindSpeedWorldRealControl->value();
	world_settings_out.volumetric_cloud_settings.edge_softness = (float)cloudEdgeSoftnessWorldRealControl->value();
	world_settings_out.volumetric_cloud_settings.horizon_fade = (float)cloudHorizonFadeWorldRealControl->value();
	world_settings_out.volumetric_cloud_settings.shape_period = (float)cloudShapePeriodWorldRealControl->value();
	world_settings_out.volumetric_cloud_settings.detail_period = (float)cloudDetailPeriodWorldRealControl->value();
	world_settings_out.volumetric_cloud_settings.max_march_dist = (float)cloudMaxMarchDistWorldRealControl->value();
	world_settings_out.volumetric_cloud_settings.wind_direction_deg = (float)cloudWindDirectionWorldRealControl->value();
	world_settings_out.cloud_lighting_settings.direct_sun_strength = (float)cloudDirectSunWorldRealControl->value();
	world_settings_out.cloud_lighting_settings.sky_light_strength = (float)cloudSkyLightWorldRealControl->value();
	world_settings_out.cloud_lighting_settings.sunset_response = (float)cloudSunsetResponseWorldRealControl->value();
	world_settings_out.cloud_lighting_settings.ground_contribution = (float)cloudGroundContributionWorldRealControl->value();
	world_settings_out.cloud_lighting_settings.underside_darkness = (float)cloudBottomDarknessWorldRealControl->value();
	world_settings_out.cloud_lighting_settings.phase_g = (float)cloudPhaseGWorldRealControl->value();
	world_settings_out.cloud_lighting_settings.phase_blend = (float)cloudPhaseBlendWorldRealControl->value();
	world_settings_out.cloud_lighting_settings.multi_scattering = (float)cloudMultiScatteringWorldRealControl->value();
	world_settings_out.cloud_lighting_settings.scattering_scale = (float)cloudScatteringWorldRealControl->value();
	world_settings_out.water_reflection_settings.cloud_reflection_enabled = waterCloudReflectionEnabledCheckBox->isChecked();
	world_settings_out.water_reflection_settings.cloud_reflection_strength = (float)cloudWaterReflectionWorldRealControl->value();
	world_settings_out.water_reflection_settings.cloud_reflection_samples = (float)waterCloudReflectionSamplesWorldRealControl->value();
	world_settings_out.water_reflection_settings.cloud_reflection_fade = (float)waterCloudReflectionFadeWorldRealControl->value();
	world_settings_out.water_surface_settings.wave_amplitude = (float)waterWaveAmplitudeWorldRealControl->value();
	world_settings_out.water_surface_settings.wave_length = (float)waterWaveLengthWorldRealControl->value();
	world_settings_out.water_surface_settings.wave_steepness = (float)waterWaveSteepnessWorldRealControl->value();
	world_settings_out.water_surface_settings.wave_speed = (float)waterWaveSpeedWorldRealControl->value();
	world_settings_out.water_surface_settings.wave_direction_deg = (float)waterWaveDirectionWorldRealControl->value();
	world_settings_out.water_surface_settings.wave_direction_spread_deg = (float)waterWaveSpreadWorldRealControl->value();
	world_settings_out.water_surface_settings.secondary_wave_scale = (float)waterSecondaryWaveScaleWorldRealControl->value();
	world_settings_out.water_surface_settings.surf_enabled = waterSurfEnabledCheckBox->isChecked();
	world_settings_out.water_surface_settings.underwater_caustics_enabled = waterCausticsEnabledCheckBox->isChecked();
	world_settings_out.water_surface_settings.surf_strength = (float)waterSurfStrengthWorldRealControl->value();
	world_settings_out.water_surface_settings.shoreline_width = (float)waterShorelineWidthWorldRealControl->value();
	world_settings_out.water_surface_settings.foam_scale = (float)waterFoamScaleWorldRealControl->value();
	world_settings_out.water_surface_settings.foam_speed = (float)waterFoamSpeedWorldRealControl->value();
	world_settings_out.water_surface_settings.foam_fade = (float)waterFoamFadeWorldRealControl->value();
}


void WorldSettingsWidget::updateControlsEditable()
{
	const bool editable = main_window && main_window->connectedToUsersWorldOrGodUser();

	for(int i=0; i<terrainSectionScrollAreaWidgetContents->layout()->count(); ++i)
	{
		QWidget* widget = terrainSectionScrollAreaWidgetContents->layout()->itemAt(i)->widget();
		TerrainSpecSectionWidget* section_widget = dynamic_cast<TerrainSpecSectionWidget*>(widget);
		if(section_widget)
			section_widget->updateControlsEditable(editable);
	}

	newTerrainSectionPushButton->setEnabled(editable);
	for(int i=0; i<4; ++i)
	{
		if(QPushButton* button = findChild<QPushButton*>(QString("terrainMaterialBrowserButton%1").arg(i)))
			button->setEnabled(editable);
		if(QPushButton* button = findChild<QPushButton*>(QString("terrainPolyHavenButton%1").arg(i)))
			button->setEnabled(editable);
	}

	detailColMapURLs0FileSelectWidget->setReadOnly(!editable);
	detailColMapURLs1FileSelectWidget->setReadOnly(!editable);
	detailColMapURLs2FileSelectWidget->setReadOnly(!editable);
	detailColMapURLs3FileSelectWidget->setReadOnly(!editable);
	detailColMapURLs0EnabledCheckBox->setEnabled(editable);
	detailColMapURLs1EnabledCheckBox->setEnabled(editable);
	detailColMapURLs2EnabledCheckBox->setEnabled(editable);
	detailColMapURLs3EnabledCheckBox->setEnabled(editable);

	detailHeightMapURLs0FileSelectWidget->setReadOnly(!editable);
	detailHeightMapURLs0EnabledCheckBox->setEnabled(editable);
	exactHeightmapCheckBox->setEnabled(editable);

	terrainSectionWidthDoubleSpinBox->setReadOnly(!editable);
	terrainHeightScaleDoubleSpinBox->setReadOnly(!editable);
	defaultTerrainZDoubleSpinBox->setReadOnly(!editable);
	waterZDoubleSpinBox->setReadOnly(!editable);
	waterCheckBox->setEnabled(editable);

	sunThetaSettingRealControl->setEnabled(editable);
	sunPhiSettingRealControl  ->setEnabled(editable);
	layer0ASpinBox->setReadOnly(!editable);
	layer0HeightScaleSpinBox->setReadOnly(!editable);
	layer1ASpinBox->setReadOnly(!editable);
	layer1HeightScaleSpinBox->setReadOnly(!editable);
	volumetricCloudsEnabledCheckBox->setEnabled(editable);
	cloudBottomZWorldRealControl->setEnabled(editable);
	cloudTopZWorldRealControl->setEnabled(editable);
	cloudCoverageWorldRealControl->setEnabled(editable);
	cloudDensityWorldRealControl->setEnabled(editable);
	cloudWindSpeedWorldRealControl->setEnabled(editable);
	cloudBottomDarknessWorldRealControl->setEnabled(editable);
	cloudEdgeSoftnessWorldRealControl->setEnabled(editable);
	cloudHorizonFadeWorldRealControl->setEnabled(editable);
	cloudShapePeriodWorldRealControl->setEnabled(editable);
	cloudDetailPeriodWorldRealControl->setEnabled(editable);
	cloudMaxMarchDistWorldRealControl->setEnabled(editable);
	cloudWindDirectionWorldRealControl->setEnabled(editable);
	cloudDirectSunWorldRealControl->setEnabled(editable);
	cloudSkyLightWorldRealControl->setEnabled(editable);
	cloudSunsetResponseWorldRealControl->setEnabled(editable);
	cloudGroundContributionWorldRealControl->setEnabled(editable);
	cloudPhaseGWorldRealControl->setEnabled(editable);
	cloudPhaseBlendWorldRealControl->setEnabled(editable);
	cloudMultiScatteringWorldRealControl->setEnabled(editable);
	cloudScatteringWorldRealControl->setEnabled(editable);
	waterCloudReflectionEnabledCheckBox->setEnabled(editable);
	cloudWaterReflectionWorldRealControl->setEnabled(editable);
	waterCloudReflectionSamplesWorldRealControl->setEnabled(editable);
	waterCloudReflectionFadeWorldRealControl->setEnabled(editable);
	waterWaveAmplitudeWorldRealControl->setEnabled(editable);
	waterWaveLengthWorldRealControl->setEnabled(editable);
	waterWaveSteepnessWorldRealControl->setEnabled(editable);
	waterWaveSpeedWorldRealControl->setEnabled(editable);
	waterWaveDirectionWorldRealControl->setEnabled(editable);
	waterWaveSpreadWorldRealControl->setEnabled(editable);
	waterSecondaryWaveScaleWorldRealControl->setEnabled(editable);
	waterSurfEnabledCheckBox->setEnabled(editable);
	waterCausticsEnabledCheckBox->setEnabled(editable);
	waterSurfStrengthWorldRealControl->setEnabled(editable);
	waterShorelineWidthWorldRealControl->setEnabled(editable);
	waterFoamScaleWorldRealControl->setEnabled(editable);
	waterFoamSpeedWorldRealControl->setEnabled(editable);
	waterFoamFadeWorldRealControl->setEnabled(editable);

	applyPushButton->setEnabled(editable);
	if(sculpting_mode_check_box)
	{
		sculpting_mode_check_box->setEnabled(editable);
		setSculptingControlsEnabled(editable && sculpting_mode_check_box->isChecked());
	}
}


void WorldSettingsWidget::newTerrainSectionPushButtonClicked()
{
	TerrainSpecSectionWidget* new_section_widget = new TerrainSpecSectionWidget(this);

	terrainSectionScrollAreaWidgetContents->layout()->addWidget(new_section_widget);

	connect(new_section_widget, SIGNAL(removeButtonClickedSignal()), this, SLOT(removeTerrainSectionButtonClickedSlot()));
	connect(new_section_widget->heightmapEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	connect(new_section_widget->maskMapEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	connect(new_section_widget->treeMaskMapEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	connect(new_section_widget->roadMaskMapEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
	connect(new_section_widget->buildingMaskMapEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(settingsChangedSlot()));
}


void WorldSettingsWidget::removeTerrainSectionButtonClickedSlot()
{
	QObject* sender_ob = QObject::sender();

	terrainSectionScrollAreaWidgetContents->layout()->removeWidget((QWidget*)sender_ob);

	sender_ob->deleteLater();
}


void WorldSettingsWidget::applySettingsSlot()
{
	settingsChangedSlot();
}


void WorldSettingsWidget::waterSurfEnabledToggledSlot(bool enabled)
{
	if(enabled)
		applyRecommendedWaterSurfSettings();
	settingsChangedSlot();
}


void WorldSettingsWidget::applyRecommendedWaterSurfSettings()
{
	// One coordinated default for new worlds and explicit surf enable. Loading
	// a saved world never resets the owner's tuning or chosen wave direction.
	const WaterSurfaceWorldSettings preset;
	SignalBlocker::setValue(waterWaveAmplitudeWorldRealControl, preset.wave_amplitude);
	SignalBlocker::setValue(waterWaveLengthWorldRealControl, preset.wave_length);
	SignalBlocker::setValue(waterWaveSteepnessWorldRealControl, preset.wave_steepness);
	SignalBlocker::setValue(waterWaveSpeedWorldRealControl, preset.wave_speed);
	SignalBlocker::setValue(waterWaveSpreadWorldRealControl, preset.wave_direction_spread_deg);
	SignalBlocker::setValue(waterSecondaryWaveScaleWorldRealControl, preset.secondary_wave_scale);
	SignalBlocker::setValue(waterSurfStrengthWorldRealControl, preset.surf_strength);
	SignalBlocker::setValue(waterShorelineWidthWorldRealControl, preset.shoreline_width);
	SignalBlocker::setValue(waterFoamScaleWorldRealControl, preset.foam_scale);
	SignalBlocker::setValue(waterFoamSpeedWorldRealControl, preset.foam_speed);
	SignalBlocker::setValue(waterFoamFadeWorldRealControl, preset.foam_fade);
}


void WorldSettingsWidget::settingsChangedSlot()
{
	try
	{
		emit settingsChangedSignal();
	}
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}
