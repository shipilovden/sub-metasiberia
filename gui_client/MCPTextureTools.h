// Qt-only local MCP asset adapter. All decoding, generation and network work
// runs on the MCP worker; no GUI object or OpenGL context is accessed here.
#pragma once
#include <QtCore/QBuffer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QDateTime>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <QtGui/QPainter>
#include <QtGui/QLinearGradient>
#include <cmath>
#include <functional>
#include <algorithm>
#include <utils/Exception.h>
#include <networking/HTTPClient.h>

namespace MCPTextures {
using ServerCall = std::function<QJsonObject(const QString&,const QJsonObject&)>;
inline void fail(const QString& message) { throw glare::Exception(message.toStdString()); }
inline double number(const QJsonObject& a,const char* key,double fallback,double lo,double hi)
{
	if(a.contains(key) && !a.value(key).isDouble()) fail(QStringLiteral("Invalid number: ")+key);
	const double v=a.value(key).toDouble(fallback);
	if(!std::isfinite(v) || v<lo || v>hi) fail(QStringLiteral("Number outside allowed range: ")+key);
	return v;
}
inline QByteArray fetch(const QUrl& url, size_t max_bytes=24*1024*1024)
{
	// No credentials, arbitrary hosts, redirects or private-network fetches.
	const QString host=url.host().toLower();
	if(url.scheme()!="https" || !url.userInfo().isEmpty() || url.port(443)!=443 ||
		(host!="api.polyhaven.com" && host!="dl.polyhaven.org" && host!="cdn.polyhaven.com")) fail("Unsupported Poly Haven URL.");
	HTTPClient client;
	client.max_data_size=max_bytes;
	client.additional_headers.push_back("User-Agent: Metasiberia/0.0.24 (AI material importer)");
	std::vector<uint8> data;
	const HTTPClient::ResponseInfo response=client.downloadFile(url.toString(QUrl::FullyEncoded).toStdString(),data);
	if(response.response_code!=200)
		fail(QStringLiteral("Poly Haven request to %1 failed (HTTP %2 %3).")
			.arg(host).arg(response.response_code).arg(QString::fromStdString(response.response_message)));
	if(data.size()>max_bytes) fail("Poly Haven response is too large.");
	return QByteArray(reinterpret_cast<const char*>(data.data()),(int)data.size());
}
inline QImage decode(QByteArray bytes)
{
	QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer);
	const QSize size=reader.size();
	if(!size.isValid() || size.width()>8192 || size.height()>8192 || (qint64)size.width()*size.height()>32*1024*1024) fail("Image dimensions exceed the decode budget.");
	reader.setAutoTransform(true); QImage image=reader.read();
	if(image.isNull()) fail("Cannot decode image: "+reader.errorString());
	if(image.width()>1024 || image.height()>1024) image=image.scaled(1024,1024,Qt::KeepAspectRatio,Qt::SmoothTransformation);
	return image.convertToFormat(QImage::Format_RGBA8888);
}
inline QJsonObject upload(const QImage& image,const ServerCall& call)
{
	QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
	if(!image.save(&buffer,"PNG")) fail("Could not encode texture.");
	if(bytes.size()>4*1024*1024) fail("Texture exceeds the PNG upload budget.");
	return call("import_texture",QJsonObject{{"png_base64",QString::fromLatin1(bytes.toBase64())}});
}
inline QJsonObject textureCatalog()
{
	static QMutex mutex; static QJsonObject catalog; static qint64 expires=0;
	QMutexLocker lock(&mutex);
	if(catalog.isEmpty() || QDateTime::currentSecsSinceEpoch()>expires) {
		catalog=QJsonDocument::fromJson(fetch(QUrl("https://api.polyhaven.com/assets?type=textures"))).object();
		if(catalog.isEmpty()) fail("Poly Haven returned an empty texture catalog.");
		expires=QDateTime::currentSecsSinceEpoch()+900;
	}
	return catalog;
}
inline QJsonObject modelCatalog()
{
	static QMutex mutex; static QJsonObject catalog; static qint64 expires=0;
	QMutexLocker lock(&mutex);
	if(catalog.isEmpty() || QDateTime::currentSecsSinceEpoch()>expires) {
		catalog=QJsonDocument::fromJson(fetch(QUrl("https://api.polyhaven.com/assets?type=models"),64*1024*1024)).object();
		if(catalog.isEmpty()) fail("Poly Haven returned an empty model catalog.");
		expires=QDateTime::currentSecsSinceEpoch()+900;
	}
	return catalog;
}
inline QJsonArray searchModels(const QString& query, int limit=48)
{
	const QJsonObject catalog=modelCatalog();
	const QStringList terms=query.trimmed().toLower().split(' ',Qt::SkipEmptyParts);
	struct SearchHit { int score; QString id; QJsonObject asset; };
	std::vector<SearchHit> hits;
	for(auto it=catalog.begin(); it!=catalog.end(); ++it) {
		const QJsonObject asset=it.value().toObject();
		if(asset.value("type").toInt(-1)!=2) continue;
		const QString id=it.key().toLower(), name=asset.value("name").toString().toLower();
		const QString category=asset.value("category").toString().toLower();
		QStringList tags;
		for(const QJsonValue& tag : asset.value("tags").toArray()) if(tag.isString()) tags.append(tag.toString().toLower());
		const QString tag_text=tags.join(' '), description=asset.value("description").toString().toLower();
		int score=0; bool match=true;
		for(const QString& term : terms) {
			if(id==term) score+=100;
			else if(name==term) score+=60;
			else if(name.contains(term)) score+=35;
			else if(tags.contains(term)) score+=25;
			else if(tag_text.contains(term)) score+=15;
			else if(category.contains(term)) score+=8;
			else if(description.contains(term)) score+=1;
			else match=false;
		}
		if(match) hits.push_back({score,it.key(),asset});
	}
	std::sort(hits.begin(),hits.end(),[](const SearchHit& a,const SearchHit& b) {
		return a.score==b.score ? a.id<b.id : a.score>b.score;
	});
	QJsonArray found;
	for(size_t i=0;i<hits.size() && found.size()<limit;++i) {
		const SearchHit& hit=hits[i];
		found.append(QJsonObject{{"id",hit.id},{"name",hit.asset.value("name")},{"category",hit.asset.value("category")},
			{"thumbnail_url",hit.asset.value("thumbnail_url")},{"description",hit.asset.value("description")},
			{"source","Poly Haven"},{"license","CC0"}});
	}
	return found;
}
inline QJsonArray textureUVScale(const QJsonObject& asset)
{
	const QJsonArray dimensions=asset.value("dimensions").toArray();
	if(dimensions.size()>=2 && dimensions[0].isDouble() && dimensions[1].isDouble()) {
		const double width_mm=dimensions[0].toDouble(),height_mm=dimensions[1].toDouble();
		if(width_mm>0 && height_mm>0) {
			// Poly Haven dimensions are millimetres; mesh UVs are measured in metres.
			const double u=1000.0/width_mm,v=1000.0/height_mm;
			if(std::isfinite(u) && std::isfinite(v) && u>=0.0001 && u<=10000 && v>=0.0001 && v<=10000)
				return QJsonArray{u,v};
		}
	}
	return QJsonArray{1,1};
}
inline QJsonObject search(const QJsonObject& args)
{
	const QJsonObject catalog=textureCatalog();
	const QStringList terms=args.value("query").toString().toLower().split(' ',Qt::SkipEmptyParts);
	const int limit=(int)number(args,"limit",12,1,30);
	struct SearchHit { int score; QString id; QJsonObject asset; };
	std::vector<SearchHit> hits;
	for(auto it=catalog.begin(); it!=catalog.end(); ++it) {
		const QJsonObject asset=it.value().toObject(); if(asset.value("type").toInt(-1)!=1) continue;
		const QString id=it.key().toLower(), name=asset.value("name").toString().toLower();
		const QString category=asset.value("category").toString().toLower();
		QStringList tags;
		for(const QJsonValue& tag : asset.value("tags").toArray()) if(tag.isString()) tags.append(tag.toString().toLower());
		const QString tag_text=tags.join(' '), description=asset.value("description").toString().toLower();
		int score=0; bool match=true;
		for(const QString& term : terms) {
			if(id==term) score+=100;
			else if(name==term) score+=60;
			else if(name.contains(term)) score+=35;
			else if(tags.contains(term)) score+=25;
			else if(tag_text.contains(term)) score+=15;
			else if(category.contains(term)) score+=8;
			else if(description.contains(term)) score+=1;
			else match=false;
		}
		if(match) hits.push_back({score,it.key(),asset});
	}
	std::sort(hits.begin(),hits.end(),[](const SearchHit& a,const SearchHit& b) {
		return a.score==b.score ? a.id<b.id : a.score>b.score;
	});
	QJsonArray found;
	for(size_t i=0;i<hits.size() && found.size()<limit;++i) {
		const SearchHit& hit=hits[i];
		found.append(QJsonObject{{"id",hit.id},{"name",hit.asset.value("name")},{"category",hit.asset.value("category")},
			{"dimensions_mm",hit.asset.value("dimensions")},{"uv_scale",textureUVScale(hit.asset)},
			{"source","Poly Haven"},{"license","CC0"}});
	}
	return QJsonObject{{"assets",found},{"hint","Search with short English material words, e.g. wood, brick, stone. Use returned id; do not invent download URLs."}};
}
inline QImage map(const QJsonObject& files,const QStringList& names,const QString& resolution,bool required=false)
{
	QString last_decode_error;
	for(const QString& name:names) {
		const QJsonObject formats=files.value(name).toObject().value(resolution).toObject();
		for(const QString& format : {QString("png"),QString("jpg")}) {
			const QJsonObject file=formats.value(format).toObject();
			if(file.value("url").isString()) {
				if(file.value("size").toDouble()>24*1024*1024) { last_decode_error="the available PNG/JPG exceeds 24 MiB"; continue; }
				const QByteArray bytes=fetch(QUrl(file.value("url").toString()));
				try { return decode(bytes); }
				catch(const glare::Exception& e) { last_decode_error=QString::fromStdString(e.what()); }
			}
		}
	}
	if(required) fail(QString("Could not load required Poly Haven map (%1) at %2: %3").arg(names.join('/'),resolution,last_decode_error.isEmpty()?QStringLiteral("no supported PNG or JPG was listed"):last_decode_error));
	return QImage();
}
inline QJsonObject download(const QJsonObject& args,const ServerCall& call)
{
	const QString id=args.value("asset_id").toString();
	if(!QRegularExpression("^[a-z0-9_]{1,100}$").match(id).hasMatch()) fail("Invalid Poly Haven asset id.");
	const QString resolution=args.value("resolution").toString("1k");
	if(resolution!="1k" && resolution!="2k") fail("Use 1k or 2k (normalised to at most 1024 pixels).");
	const QJsonObject files=QJsonDocument::fromJson(fetch(QUrl("https://api.polyhaven.com/files/"+id))).object();
	if(files.isEmpty()) fail("Poly Haven returned no files for texture asset: "+id);
	const QJsonObject asset=textureCatalog().value(id).toObject();
	if(!asset.isEmpty() && asset.value("type").toInt(-1)!=1) fail("Poly Haven asset is not a texture: "+id);
	const QImage color=map(files,{"Diffuse","diff","diffuse","Color"},resolution,true);
	QImage packed=map(files,{"arm"},resolution);
	if(packed.isNull()) {
		QImage rough=map(files,{"Rough"},resolution),metal=map(files,{"Metal"},resolution);
		packed=QImage(color.size(),QImage::Format_RGB32);
		if(!rough.isNull()) rough=rough.scaled(color.size()); if(!metal.isNull()) metal=metal.scaled(color.size());
		for(int y=0;y<packed.height();++y) for(int x=0;x<packed.width();++x)
			packed.setPixel(x,y,qRgb(255,rough.isNull()?200:qRed(rough.pixel(x,y)),metal.isNull()?0:qRed(metal.pixel(x,y))));
	}
	const QImage normal=map(files,{"nor_gl"},resolution);
	QJsonObject material{{"color",QJsonArray{1,1,1}},{"roughness",1},{"metallic",1},
		{"color_texture",upload(color,call).value("texture_url")},{"metallic_roughness_texture",upload(packed,call).value("texture_url")},
		{"uv_scale",textureUVScale(asset)}};
	if(!normal.isNull()) material.insert("normal_texture",upload(normal,call).value("texture_url"));
	return QJsonObject{{"material",material},{"dimensions_mm",asset.value("dimensions")},{"uv_scale",material.value("uv_scale")},
		{"maps_loaded",QJsonObject{{"color",true},{"metallic_roughness",true},{"normal",!normal.isNull()}}},
		{"source","Poly Haven"},{"license","CC0"},{"asset_id",id},{"applied",false}};
}
inline QColor color(const QJsonObject& args,const char* key,const char* fallback)
{
	QColor c(args.value(key).toString(fallback)); if(!c.isValid()) fail(QStringLiteral("Invalid colour: ")+key); return c;
}
inline QJsonObject procedural(const QJsonObject& args,const ServerCall& call)
{
	const QString pattern=args.value("pattern").toString("noise");
	if(!QStringList{"noise","wood","marble","brick","checker"}.contains(pattern)) fail("Unknown procedural pattern.");
	const int size=(int)number(args,"resolution",512,64,1024);
	const double repeat_value=number(args,"repeats",8,2,64);
	if(repeat_value!=std::floor(repeat_value)) fail("repeats must be an integer.");
	const int repeats=(int)repeat_value;
	if((pattern=="brick" || pattern=="checker") && repeats%2) fail("Use an even repeats value for seamless brick/checker textures.");
	const double seed=number(args,"seed",1,0,1000000),roughness=number(args,"roughness",.65,0,1),metallic=number(args,"metallic",0,0,1);
	const QColor a=color(args,"color_a","#5c3820"),b=color(args,"color_b","#bc9060");
	QImage diffuse(size,size,QImage::Format_RGB32),packed(size,size,QImage::Format_RGB32),normal(size,size,QImage::Format_RGB32);
	std::vector<double> heights((size_t)size*size);
	const double tau=6.283185307179586;
	// Periodic harmonics give exactly tileable noise and wood/marble veins.
	for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
		const double u=(double)x/size,v=(double)y/size;
		double noise=0,weight=0;
		for(int k=1;k<=5;++k) { const double w=1.0/k; noise+=w*std::sin(tau*(u*(k*3)+v*(k*2))+seed*k)*std::cos(tau*(u*k-v*(k*4))+seed/(k+1));weight+=w; }
		noise=.5+.5*noise/weight; double h=noise;
		if(pattern=="wood") h=.5+.5*std::sin(tau*u*repeats+noise*8);
		if(pattern=="marble") h=std::pow(.5+.5*std::sin(tau*(u+v)*repeats+noise*5),4);
		if(pattern=="checker") h=((int)(u*repeats)+(int)(v*repeats))%2;
		if(pattern=="brick") { const int row=(int)(v*repeats); const double bx=u*repeats+(row%2)*.5,by=v*repeats; h=(bx-std::floor(bx)<.07 || by-std::floor(by)<.09) ? 0 : .65+.3*noise; }
		heights[(size_t)y*size+x]=h;
		diffuse.setPixel(x,y,qRgb((int)(a.red()*(1-h)+b.red()*h),(int)(a.green()*(1-h)+b.green()*h),(int)(a.blue()*(1-h)+b.blue()*h)));
		packed.setPixel(x,y,qRgb(255,qBound(0,(int)(255*(roughness+.15*(noise-.5))),255),(int)(255*metallic)));
	}
	for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
		const double dx=(heights[(size_t)y*size+(x+1)%size]-heights[(size_t)y*size+(x+size-1)%size])*3;
		const double dy=(heights[(size_t)((y+1)%size)*size+x]-heights[(size_t)((y+size-1)%size)*size+x])*3;
		const double len=std::sqrt(dx*dx+dy*dy+1); normal.setPixel(x,y,qRgb((int)(127.5-127.5*dx/len),(int)(127.5+127.5*dy/len),(int)(127.5+127.5/len)));
	}
	return QJsonObject{{"material",QJsonObject{{"color",QJsonArray{1,1,1}},{"roughness",1},{"metallic",1},
		{"color_texture",upload(diffuse,call).value("texture_url")},{"metallic_roughness_texture",upload(packed,call).value("texture_url")},
		{"normal_texture",upload(normal,call).value("texture_url")}}},{"applied",false},{"generator","local procedural; not GPT Image"}};
}
inline QJsonObject importImage(const QJsonObject& args,const ServerCall& call)
{
	const QString path=args.value("path").toString(); QFileInfo info(path);
	if(!info.isAbsolute() || !info.isFile() || !QStringList{"png","jpg","jpeg","webp"}.contains(info.suffix().toLower()) || info.size()>24*1024*1024)
		fail("Expected an existing PNG/JPEG/WebP image, at most 24 MiB. Import only images attached/requested by the user or generated for this task.");
	QFile file(path); if(!file.open(QIODevice::ReadOnly)) fail("Cannot open image.");
	return upload(decode(file.read(24*1024*1024+1)),call);
}
inline QJsonObject drawImage(const QJsonObject& args,const ServerCall& call)
{
	const int width=(int)number(args,"width",512,64,1024),height=(int)number(args,"height",512,64,1024);
	QImage image(width,height,QImage::Format_ARGB32); image.fill(color(args,"background","#ffffff"));
	QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::TextAntialiasing);
	if(args.contains("background_bottom")) {
		QLinearGradient gradient(0,0,0,height); gradient.setColorAt(0,color(args,"background","#ffffff")); gradient.setColorAt(1,color(args,"background_bottom","#000000"));
		painter.fillRect(image.rect(),gradient);
	}
	const QJsonArray layers=args.value("layers").toArray(); if(layers.size()>256) fail("Use at most 256 drawing layers.");
	for(const QJsonValue& value:layers) {
		const QJsonObject layer=value.toObject(); const QString kind=layer.value("shape").toString();
		const double x=number(layer,"x",0,-2048,2048),y=number(layer,"y",0,-2048,2048),w=number(layer,"width",100,0,2048),h=number(layer,"height",100,0,2048);
		painter.setBrush(color(layer,"fill","#000000"));
		painter.setPen(layer.contains("stroke") ? QPen(color(layer,"stroke","#000000"),number(layer,"stroke_width",1,0,64)) : QPen(Qt::NoPen));
		if(kind=="rectangle") painter.drawRoundedRect(QRectF(x,y,w,h),number(layer,"radius",0,0,1024),number(layer,"radius",0,0,1024));
		else if(kind=="ellipse") painter.drawEllipse(QRectF(x,y,w,h));
		else if(kind=="polygon") {
			QPolygonF polygon; const QJsonArray points=layer.value("points").toArray();
			if(points.size()<3 || points.size()>256) fail("Polygon requires 3..256 points.");
			for(const QJsonValue& point:points) { const QJsonArray p=point.toArray();
				if(p.size()!=2 || !p[0].isDouble() || !p[1].isDouble() || std::abs(p[0].toDouble())>2048 || std::abs(p[1].toDouble())>2048) fail("Invalid polygon point.");
				polygon.append(QPointF(p[0].toDouble(),p[1].toDouble())); }
			painter.drawPolygon(polygon);
		} else if(kind=="text") {
			const QString text=layer.value("text").toString(); if(text.size()>512) fail("Text layer is too long.");
			QFont font("Arial"); font.setPixelSize((int)number(layer,"font_size",32,6,256)); font.setBold(layer.value("bold").toBool());
			painter.setFont(font); painter.setPen(color(layer,"fill","#000000"));
			painter.drawText(QRectF(x,y,w,h),Qt::AlignCenter | Qt::TextWordWrap,text);
		} else fail("Unknown image layer shape.");
	}
	painter.end(); QJsonObject result=upload(image,call); result.insert("generator","local vector drawing; not GPT Image"); return result;
}
inline QJsonArray schemas()
{
	return QJsonDocument::fromJson(R"json([
{"name":"search_polyhaven_textures","description":"Search CC0 PBR textures from Poly Haven. Requires enabling Poly Haven in AI Settings. Use short English material keywords.","inputSchema":{"type":"object","properties":{"query":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":30}},"required":["query"],"additionalProperties":false}},
{"name":"download_polyhaven_texture","description":"Download a complete Poly Haven PBR set: color/albedo, packed linear metallic-roughness (G=roughness,B=metallic), and OpenGL normal when available; upload maps and return their material recipe plus maps_loaded status. Then apply all available maps together to the requested material slot using update_object while preserving other slots and uv_scale. Does not modify world geometry.","inputSchema":{"type":"object","properties":{"asset_id":{"type":"string"},"resolution":{"type":"string","enum":["1k","2k"]}},"required":["asset_id"],"additionalProperties":false}},
{"name":"generate_procedural_texture","description":"Generate tileable PBR maps locally: colour, packed roughness/metallic and OpenGL normal. Returns a material recipe; apply to chosen slots with update_object. This is mathematical procedural generation, not neural image generation.","inputSchema":{"type":"object","properties":{"pattern":{"type":"string","enum":["noise","wood","marble","brick","checker"]},"resolution":{"type":"integer","minimum":64,"maximum":1024},"repeats":{"type":"integer","minimum":2,"maximum":64,"description":"Use even repeats for seamless brick/checker."},"seed":{"type":"integer"},"color_a":{"type":"string"},"color_b":{"type":"string"},"roughness":{"type":"number"},"metallic":{"type":"number"}},"required":["pattern"],"additionalProperties":false}},
{"name":"import_image","description":"Import an actual local image explicitly attached/requested by the user or generated for this task. Reads PNG/JPEG/WebP only, normalises to at most 1024 pixels and uploads a PNG resource. Do not search personal files. Then create_image places it in the world, or use its texture_url in a material. Never invent a path or a generated image.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false}},
{"name":"draw_image","description":"Create an actual PNG illustration, sign, poster or diagram from ordered vector layers. Coordinates in pixels, origin top-left. Supports gradient background, filled rectangles/ellipses/polygons and text. Returns texture_url; create_image places it in the world. This is local drawing, not neural image generation.","inputSchema":{"type":"object","properties":{"width":{"type":"integer"},"height":{"type":"integer"},"background":{"type":"string"},"background_bottom":{"type":"string"},"layers":{"type":"array","maxItems":256,"items":{"type":"object","properties":{"shape":{"type":"string","enum":["rectangle","ellipse","polygon","text"]},"x":{"type":"number"},"y":{"type":"number"},"width":{"type":"number"},"height":{"type":"number"},"radius":{"type":"number"},"fill":{"type":"string"},"stroke":{"type":"string"},"stroke_width":{"type":"number"},"points":{"type":"array","items":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2}},"text":{"type":"string"},"font_size":{"type":"number"},"bold":{"type":"boolean"}},"required":["shape"],"additionalProperties":false}}},"required":["layers"],"additionalProperties":false}}
])json").array();
}
inline bool handles(const QString& name) { return name=="search_polyhaven_textures" || name=="download_polyhaven_texture" || name=="generate_procedural_texture" || name=="import_image" || name=="draw_image"; }
inline QJsonObject call(const QString& name,const QJsonObject& args,bool poly_enabled,const ServerCall& server)
{
	if(name.contains("polyhaven") && !poly_enabled) fail("Poly Haven is disabled. Enable it in the AI Settings tab.");
	if(name=="search_polyhaven_textures") return search(args);
	if(name=="download_polyhaven_texture") return download(args,server);
	if(name=="generate_procedural_texture") return procedural(args,server);
	if(name=="draw_image") return drawImage(args,server);
	return importImage(args,server);
}
}
