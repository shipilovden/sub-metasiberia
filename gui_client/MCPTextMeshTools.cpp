#include "MCPTextMeshTools.h"
#include "GUIClient.h"
#include "UIInterface.h"
#include "FontCoverage.h"
#include "../shared/ObjectCopyPermissions.h"
#include "../shared/ResourceManager.h"
#include <graphics/BatchedMesh.h>
#include <graphics/TextRenderer.h>
#include <utils/UTF8Utils.h>
#include <utils/Exception.h>
#include <QtCore/QJsonDocument>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTemporaryDir>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtGui/QPainterPath>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace MCPTextMeshTools
{
namespace
{
void fail(const char* message) { throw glare::Exception(message); }

bool canEditBounds(GUIClient& client, const WorldObject& object, const js::AABBox& bounds)
{
	bool in_parcel=false;
	if(!client.objectModificationAllowed(object) || !ObjectCopyPermissions::finiteBounds(bounds) ||
		!client.haveObjectWritePermissions(object,bounds,in_parcel) || !in_parcel) return false;
	if(client.logged_in_user_id == UserID(0) || client.connected_world_details.owner_id==client.logged_in_user_id) return true;
	for(const auto& entry : client.world_state->parcels)
		if(entry.second->userHasWritePerms(client.logged_in_user_id) && ObjectCopyPermissions::parcelContainsBounds(*entry.second,bounds)) return true;
	return false;
}

struct OutlineBuilder
{
	QPainterPath path;
	static QPointF point(const FT_Vector* v) { return QPointF(v->x / 64.0, v->y / 64.0); }
	static int move(const FT_Vector* p, void* data)
	{
		auto& b = *static_cast<OutlineBuilder*>(data);
		b.path.closeSubpath(); b.path.moveTo(point(p)); return 0;
	}
	static int line(const FT_Vector* p, void* data) { static_cast<OutlineBuilder*>(data)->path.lineTo(point(p)); return 0; }
	static int quadratic(const FT_Vector* c, const FT_Vector* p, void* data)
	{
		static_cast<OutlineBuilder*>(data)->path.quadTo(point(c), point(p)); return 0;
	}
	static int cubic(const FT_Vector* a, const FT_Vector* b, const FT_Vector* p, void* data)
	{
		static_cast<OutlineBuilder*>(data)->path.cubicTo(point(a), point(b), point(p)); return 0;
	}
};

struct Edge
{
	QPointF a, b;
	double xAt(double y) const { return a.x() + (b.x() - a.x()) * ((y - a.y()) / (b.y() - a.y())); }
};

struct Vertex { float x, y, z, nx, ny, nz, u, v; };
struct MeshBuilder
{
	std::vector<Vertex> face_vertices;
	std::vector<Vertex> side_vertices;
	QElapsedTimer timer;
	MeshBuilder() { timer.start(); }
	void checkTime() const { if(timer.elapsed()>2000) fail("Text conversion exceeded its 2 second geometry budget. Shorten the text or use a simpler font."); }
	void triangle(const Vec3f& a, const Vec3f& b, const Vec3f& c, bool side=false)
	{
		const Vec3f cross = crossProduct(b - a, c - a);
		if(!std::isfinite(cross.x) || !std::isfinite(cross.y) || !std::isfinite(cross.z)) fail("Text outline contains invalid coordinates.");
		if(cross.length2() < 1.0e-18f) return;
		if(face_vertices.size() + side_vertices.size() > 599997) fail("Text mesh exceeds 200000 triangles. Shorten the text or choose a simpler font.");
		const Vec3f n = normalise(cross);
		std::vector<Vertex>& destination = side ? side_vertices : face_vertices;
		for(const Vec3f& p : {a,b,c}) destination.push_back({p.x,p.y,p.z,n.x,n.y,n.z,p.x,p.y});
	}
	void cap(const std::vector<QPointF>& boundary, float depth)
	{
		if(boundary.size() < 3) return;
		QPointF middle;
		for(const auto& p : boundary) { checkTime(); middle += p; }
		middle /= boundary.size();
		for(size_t i=0;i<boundary.size();++i)
		{
			checkTime();
			const auto& a=boundary[i]; const auto& b=boundary[(i+1)%boundary.size()];
			triangle(Vec3f((float)middle.x(),(float)middle.y(),depth), Vec3f((float)a.x(),(float)a.y(),depth), Vec3f((float)b.x(),(float)b.y(),depth));
			triangle(Vec3f((float)middle.x(),(float)middle.y(),0), Vec3f((float)b.x(),(float)b.y(),0), Vec3f((float)a.x(),(float)a.y(),0));
		}
	}
};

// Flatten Qt's cubic path elements directly. QPainterPath::simplified() can
// perform an unbounded boolean-union operation on decorative font outlines,
// which used to block the GUI thread before the geometry budget was checked.
std::vector<std::vector<QPointF>> flattenPath(const QPainterPath& path, const QPointF& origin, MeshBuilder& mesh)
{
	constexpr int curve_steps = 8;
	constexpr size_t max_edges = 1024;
	std::vector<std::vector<QPointF>> contours;
	std::vector<QPointF>* contour = nullptr;
	QPointF current;
	auto addPoint = [&](const QPointF& p) {
		mesh.checkTime();
		if(!contour) fail("The text outline does not start with a contour.");
		if(contour->empty() || contour->back() != p)
		{
			contour->push_back(p / 42.0 + origin);
			if(contour->size() > max_edges + 1) fail("A glyph outline is too complex for bounded text conversion.");
		}
	};
	for(int i=0; i<path.elementCount(); ++i)
	{
		mesh.checkTime();
		const QPainterPath::Element e = path.elementAt(i);
		if(e.type == QPainterPath::MoveToElement)
		{
			contours.emplace_back();
			contour = &contours.back();
			current = QPointF(e.x, e.y);
			addPoint(current);
		}
		else if(e.type == QPainterPath::LineToElement)
		{
			current = QPointF(e.x, e.y);
			addPoint(current);
		}
		else if(e.type == QPainterPath::CurveToElement)
		{
			if(i + 2 >= path.elementCount() || path.elementAt(i + 1).type != QPainterPath::CurveToDataElement ||
				path.elementAt(i + 2).type != QPainterPath::CurveToDataElement)
				fail("The font returned an invalid cubic outline.");
			const QPointF p0 = current;
			const QPointF p1(e.x, e.y);
			const auto e2 = path.elementAt(i + 1), e3 = path.elementAt(i + 2);
			const QPointF p2(e2.x, e2.y), p3(e3.x, e3.y);
			for(int step=1; step<=curve_steps; ++step)
			{
				const double t = (double)step / curve_steps, u = 1.0 - t;
				addPoint(p0*(u*u*u) + p1*(3*u*u*t) + p2*(3*u*t*t) + p3*(t*t*t));
			}
			current = p3;
			i += 2;
		}
		else fail("The font returned an unsupported outline element.");
	}
	return contours;
}

// Horizontal decomposition supports holes and disconnected contours. Each cap
// boundary is split at every contour intersection on that row, so T junctions
// between neighbouring slabs do not leave open seams after position welding.
void extrudeGlyph(const QPainterPath& input, const QPointF& origin, float depth, MeshBuilder& mesh)
{
	if(input.elementCount() > 8192) fail("A glyph outline exceeds the safe conversion complexity limit.");
	std::vector<Edge> edges;
	std::vector<double> levels;
	for(const auto& contour : flattenPath(input, origin, mesh))
		for(size_t i=0;i<contour.size();++i)
		{
			const QPointF a=contour[i], b=contour[(i+1)%contour.size()];
			if(a==b) continue;
			edges.push_back({a,b}); levels.push_back(a.y()); levels.push_back(b.y());
		}
	if(edges.size()>1024) fail("A glyph outline is too complex for bounded text conversion.");
	std::sort(levels.begin(),levels.end());
	levels.erase(std::unique(levels.begin(),levels.end()),levels.end());
	auto contains = [&](const QPointF& point) {
		bool inside = false;
		for(const auto& edge : edges)
		{
			mesh.checkTime();
			if((edge.a.y() > point.y()) != (edge.b.y() > point.y()) &&
				point.x() < edge.xAt(point.y())) inside = !inside;
		}
		return inside;
	};
	auto rowPoints = [&](double y) {
		std::vector<double> xs;
		for(const auto& e : edges)
		{
			mesh.checkTime();
			if(y>=std::min(e.a.y(),e.b.y()) && y<=std::max(e.a.y(),e.b.y()))
			{
				if(e.a.y()==e.b.y()) { xs.push_back(e.a.x()); xs.push_back(e.b.x()); }
				else xs.push_back(e.xAt(y));
			}
		}
		std::sort(xs.begin(),xs.end()); xs.erase(std::unique(xs.begin(),xs.end()),xs.end()); return xs;
	};
	for(size_t row=1;row<levels.size();++row)
	{
		mesh.checkTime();
		const double lo=levels[row-1], hi=levels[row], mid=(lo+hi)*0.5;
		std::vector<const Edge*> active;
		for(const auto& e:edges)
		{
			mesh.checkTime();
			if(mid>std::min(e.a.y(),e.b.y()) && mid<std::max(e.a.y(),e.b.y())) active.push_back(&e);
		}
		std::sort(active.begin(),active.end(),[&](const Edge* a,const Edge* b){return a->xAt(mid)<b->xAt(mid);});
		if(active.size()%2) fail("The glyph outline could not be closed reliably.");
		const auto bottom=rowPoints(lo), top=rowPoints(hi);
		for(size_t k=0;k<active.size();k+=2)
		{
			const double bl=active[k]->xAt(lo), br=active[k+1]->xAt(lo), tl=active[k]->xAt(hi), tr=active[k+1]->xAt(hi);
			std::vector<QPointF> ring{QPointF(bl,lo)};
			for(double x:bottom) if(x>bl && x<br) ring.push_back(QPointF(x,lo));
			ring.push_back(QPointF(br,lo)); ring.push_back(QPointF(tr,hi));
			for(auto it=top.rbegin();it!=top.rend();++it) if(*it>tl && *it<tr) ring.push_back(QPointF(*it,hi));
			ring.push_back(QPointF(tl,hi)); mesh.cap(ring,depth);
		}
	}
	for(const auto& e:edges)
	{
		mesh.checkTime();
		std::vector<double> cuts{0,1};
		if(e.a.y()!=e.b.y()) for(double y:levels) { mesh.checkTime(); const double t=(y-e.a.y())/(e.b.y()-e.a.y()); if(t>0&&t<1) cuts.push_back(t); }
		else for(double x:rowPoints(e.a.y())) { const double t=(x-e.a.x())/(e.b.x()-e.a.x()); if(t>0&&t<1) cuts.push_back(t); }
		std::sort(cuts.begin(),cuts.end());
		const QPointF d=e.b-e.a, left(-d.y(),d.x());
		const double len=std::hypot(left.x(),left.y());
		const QPointF test=((e.a+e.b)*0.5+left*(1.0e-5/len)-origin)*42.0;
		const bool inside_left=contains(test);
		for(size_t i=1;i<cuts.size();++i)
		{
			QPointF a=e.a+d*cuts[i-1], b=e.a+d*cuts[i];
			if(!inside_left) std::swap(a,b);
			const Vec3f p((float)a.x(),(float)a.y(),0), q((float)b.x(),(float)b.y(),0), dz(0,0,depth);
			mesh.triangle(p,q,q+dz,true); mesh.triangle(p,q+dz,p+dz,true);
		}
	}
}

BatchedMeshRef build(const std::string& text, const std::string& font_path, const std::string& fallback_font_path, float depth)
{
	if(text.empty() || UTF8Utils::numCodePointsInString(text)>256) fail("Text conversion requires 1..256 Unicode characters.");
	TextRendererRef renderer = new TextRenderer();
	TextRendererFontFaceRef font = new TextRendererFontFace(renderer,font_path,42);
	TextRendererFontFaceRef fallback_font = font;
	if(fallback_font_path != font_path)
	{
		fallback_font = new TextRendererFontFace(renderer,fallback_font_path,42);
		if(FT_Select_Charmap(fallback_font->face, FT_ENCODING_UNICODE) != 0)
			fail("The bundled fallback font has no Unicode character map.");
	}
	if(FT_Select_Charmap(font->face, FT_ENCODING_UNICODE) != 0)
	{
		if(fallback_font == font) fail("The selected font has no usable Unicode character map.");
		font = fallback_font;
	}
	MeshBuilder builder;
	QPointF pen;
	size_t outline_points=0;
	for(size_t i=0;i<text.size();)
	{
		const size_t count=UTF8Utils::numBytesForChar(text[i]);
		if(i+count>text.size()) fail("Invalid UTF-8 text.");
		const uint32 cp=UTF8Utils::codePointForUTF8CharString(string_view(text.data()+i,count)); i+=count;
		if(cp=='\n') { pen.setX(0); pen.setY(pen.y()-1.2); continue; }
		if(cp=='\r') continue;
		FT_Face glyph_face=font->face;
		FT_UInt glyph=FT_Get_Char_Index(glyph_face,cp);
		if(!glyph)
		{
			glyph_face=fallback_font->face;
			glyph=FT_Get_Char_Index(glyph_face,cp);
		}
		if(!glyph)
		{
			glyph=FT_Get_Char_Index(glyph_face,'?');
			if(!glyph) fail("Neither the selected font nor the bundled fallback font contains this character.");
		}
		if(FT_Load_Glyph(glyph_face,glyph,FT_LOAD_DEFAULT|FT_LOAD_NO_BITMAP)) fail("Could not load a text glyph outline.");
		if(glyph_face->glyph->format!=FT_GLYPH_FORMAT_OUTLINE && glyph_face!=fallback_font->face && fallback_font->face!=font->face)
		{
			glyph_face=fallback_font->face;
			glyph=FT_Get_Char_Index(glyph_face,cp);
			if(!glyph || FT_Load_Glyph(glyph_face,glyph,FT_LOAD_DEFAULT|FT_LOAD_NO_BITMAP) || glyph_face->glyph->format!=FT_GLYPH_FORMAT_OUTLINE)
				glyph=0;
		}
		if(!glyph || glyph_face->glyph->format!=FT_GLYPH_FORMAT_OUTLINE) fail("A text glyph has no extrudable outline.");
		outline_points+=glyph_face->glyph->outline.n_points;
		if(glyph_face->glyph->outline.n_points>4096 || outline_points>32768) fail("Text outlines exceed the conversion complexity budget.");
		builder.checkTime();
		const double advance=glyph_face->glyph->advance.x/(64.0*42.0);
		OutlineBuilder outline; outline.path.setFillRule(Qt::WindingFill);
		FT_Outline_Funcs funcs{OutlineBuilder::move,OutlineBuilder::line,OutlineBuilder::quadratic,OutlineBuilder::cubic,0,0};
		if(FT_Outline_Decompose(&glyph_face->glyph->outline,&funcs,&outline)) fail("Could not extract a text glyph outline.");
		outline.path.closeSubpath();
		if(!outline.path.isEmpty()) extrudeGlyph(outline.path,pen,depth,builder);
		pen.rx()+=advance;
	}
	if(builder.face_vertices.empty() || builder.side_vertices.empty()) fail("Text contains no visible outline geometry.");
	BatchedMeshRef mesh = new BatchedMesh();
	mesh->vert_attributes.push_back({BatchedMesh::VertAttribute_Position,BatchedMesh::ComponentType_Float,0});
	mesh->vert_attributes.push_back({BatchedMesh::VertAttribute_Normal,BatchedMesh::ComponentType_Float,12});
	mesh->vert_attributes.push_back({BatchedMesh::VertAttribute_UV_0,BatchedMesh::ComponentType_Float,24});
	const size_t face_vertex_count = builder.face_vertices.size();
	std::vector<Vertex> vertices;
	vertices.reserve(face_vertex_count + builder.side_vertices.size());
	vertices.insert(vertices.end(), builder.face_vertices.begin(), builder.face_vertices.end());
	vertices.insert(vertices.end(), builder.side_vertices.begin(), builder.side_vertices.end());
	mesh->vertex_data.resize(vertices.size()*sizeof(Vertex));
	std::memcpy(mesh->vertex_data.data(),vertices.data(),mesh->vertex_data.size());
	js::Vector<uint32,16> indices(vertices.size());
	mesh->aabb_os=js::AABBox::emptyAABBox();
	for(size_t i=0;i<vertices.size();++i) {
		indices[i]=(uint32)i; const auto& v=vertices[i]; mesh->aabb_os.enlargeToHoldPoint(Vec4f(v.x,v.y,v.z,1));
	}
	mesh->setIndexDataFromIndices(indices,vertices.size());
	mesh->batches.push_back({0,(uint32)face_vertex_count,0});
	mesh->batches.push_back({(uint32)face_vertex_count,(uint32)(vertices.size()-face_vertex_count),1});
	mesh->checkValidAndSanitiseMesh();
	return mesh;
}

std::string fontTag(const std::string& font_name)
{
	static const char digits[] = "0123456789abcdef";
	std::string result;
	result.reserve(font_name.size() * 2);
	for(unsigned char c : font_name)
	{
		result.push_back(digits[c >> 4]);
		result.push_back(digits[c & 15]);
	}
	return result.empty() ? "44656661756c74" : result; // Default
}

std::string fontNameFromTag(const std::string& tag)
{
	if(tag.empty() || (tag.size() & 1)) return std::string();
	auto nibble = [](char c) -> int {
		if(c >= '0' && c <= '9') return c - '0';
		if(c >= 'a' && c <= 'f') return c - 'a' + 10;
		if(c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	std::string result;
	result.reserve(tag.size() / 2);
	for(size_t i=0; i<tag.size(); i+=2)
	{
		const int high=nibble(tag[i]), low=nibble(tag[i+1]);
		if(high < 0 || low < 0) return std::string();
		result.push_back((char)((high << 4) | low));
	}
	return result;
}

std::string meshResourceFilename(const std::string& font_name)
{
	return "mcp_model_text_mesh_f_" + fontTag(font_name) + ".bmesh";
}
}

bool isConvertedTextMeshURL(const std::string& model_url)
{
	return hasPrefix(model_url, "mcp_model_text_mesh_f_") || hasPrefix(model_url, "text_mesh_");
}

std::string convertedTextMeshFontName(const std::string& model_url)
{
	const std::string prefix = "mcp_model_text_mesh_f_";
	if(!hasPrefix(model_url, prefix)) return std::string();
	const size_t end = model_url.rfind('_');
	if(end == std::string::npos || end <= prefix.size()) return std::string();
	return fontNameFromTag(model_url.substr(prefix.size(), end - prefix.size()));
}

void rebuildConvertedTextMeshForFont(GUIClient& client, WorldObject& object)
{
	if(object.object_type != WorldObject::ObjectType_Generic || !isConvertedTextMeshURL(toStdString(object.model_url)))
		fail("This object is not a converted 3D text mesh.");
	const double z_scale = std::abs(object.scale.z);
	const js::AABBox old_bounds = object.getAABBOS();
	const double depth = (old_bounds.max_.x[2] - old_bounds.min_.x[2]) * z_scale;
	if(!std::isfinite(depth) || depth < 0.001 || depth > 10.0 || z_scale < 1.0e-6)
		fail("The 3D text has an invalid extrusion depth.");
	const std::string font_path = client.resolvedWorldTextFontPath(object);
	WorldObject default_font_object;
	default_font_object.text_font = "Default";
	std::string fallback_font_path = client.resolvedWorldTextFontPath(default_font_object);
	static FontCoverage coverage;
	const QVector<uint> codepoints = QString::fromStdString(object.content).toUcs4();
	if(!coverage.supports(fallback_font_path, codepoints))
	{
		const QDir font_dir(QString::fromStdString(client.resources_dir_path + "/fonts"));
		for(const QFileInfo& entry : font_dir.entryInfoList({"*.ttf", "*.otf", "*.woff", "*.fon"}, QDir::Files, QDir::Name))
			if(coverage.supports(entry.absoluteFilePath().toStdString(), codepoints)) { fallback_font_path = entry.absoluteFilePath().toStdString(); break; }
	}
	BatchedMeshRef mesh = build(object.content, font_path, fallback_font_path, (float)(depth / z_scale));
	QTemporaryDir temp;
	if(!temp.isValid()) fail("Could not create a temporary directory for text mesh.");
	const QString mesh_filename = QString::fromStdString(meshResourceFilename(object.text_font));
	const std::string path = temp.filePath(mesh_filename).toStdString();
	mesh->writeToFile(path);
	const URLString model_url = client.resource_manager->copyLocalFileToResourceDirAndReturnURL(path);
	client.enqueueResourceUpload(model_url);
	object.model_url = model_url;
	object.max_model_lod_level = 0;
	object.setAABBOS(mesh->aabb_os);
	object.changed_flags |= WorldObject::MODEL_URL_CHANGED;
	BitUtils::zeroBit(object.changed_flags, WorldObject::TEXT_FONT_CHANGED);
}

bool handles(const QString& name) { return name=="convert_text_to_mesh" || name=="get_text_object_info"; }
QJsonArray schemas()
{
	return QJsonDocument::fromJson(R"json([{
"name":"get_text_object_info",
"description":"Read a loaded Text object's exact UID, current text, chosen font, font coverage and edit permission from the native client. Omit uid to inspect the currently selected object, for requests such as 'make this text 3D'. Read-only. Returns the current object type even after conversion. font_coverage reports missing_characters and an optional compatible_font containing every current character. Conversion preserves every character by taking absent glyph outlines from a bundled compatible fallback font; explain that those characters may have a different typeface, and never drop the text. Coverage is not a guarantee that bitmap/emoji glyphs have extrudable outlines. Use its exact uid in convert_text_to_mesh; do not guess the selected object from a screenshot.",
"inputSchema":{"type":"object","properties":{"uid":{"type":"string","pattern":"^[0-9]+$"}},"additionalProperties":false}
},{
"name":"convert_text_to_mesh",
"description":"Convert an existing world Text object's real font outlines into an editable solid native .bmesh, replacing that same UID. Read the object first. Preserves text content, font name, position, rotation, materials and scripts; text is no longer editable as letters after conversion (Undo restores Text). Front/back caps use material slot 0 and extruded sides use a separate darker copy in material slot 1, both editable in the Materials panel. Extrudes along local +Z from the existing text plane; depth_m is world thickness accounting for local Z scale. Supports holes, Cyrillic and multiline text when the font contains Unicode outline glyphs. Maximum 256 characters, no bevel or bitmap/color emoji. Uses the connected Qt client's exact bundled font and native resource upload/update path with edit/placement permission checks. Returns queued, not server confirmation: use get_object and render_view to confirm Generic type/model_url and visible result before claiming completion. Never invent a successful conversion from this queue acknowledgement alone.",
"inputSchema":{"type":"object","properties":{"uid":{"type":"string","pattern":"^[0-9]+$"},"depth_m":{"type":"number","minimum":0.001,"maximum":10}},"required":["uid"],"additionalProperties":false}
}])json").array();
}

QJsonObject handle(GUIClient& client, const QString& name, const QJsonObject& args)
{
	if(!handles(name)) fail("Unknown text mesh tool.");
	if(client.world_state.isNull()||client.resource_manager.isNull()||!client.logged_in_user_id.valid()) fail("Connect and log in before inspecting or converting text.");
	WorldStateLock lock(client.world_state->mutex);
	QString uid_text=args.value("uid").toString();
	if(name=="get_text_object_info" && !args.contains("uid")) {
		if(client.selected_ob.isNull()) return QJsonObject{{"selected",false}};
		uid_text=QString::number(client.selected_ob->uid.value());
	}
	bool valid=false; const qulonglong uid=uid_text.toULongLong(&valid);
	const double depth=args.value("depth_m").toDouble(0.1);
	if(!valid||uid_text!=QString::number(uid)||
		(args.contains("depth_m")&&!args.value("depth_m").isDouble())||!std::isfinite(depth)||depth<0.001||depth>10)
		fail("Expected a decimal string uid and depth_m in 0.001..10 metres.");
	auto found=client.world_state->objects.find(UID(uid));
	if(found==client.world_state->objects.end()||found.getValue()->state==WorldObject::State_Dead) fail("Text object is not loaded in this client.");
	WorldObjectRef object=found.getValue();
	const bool can_edit=canEditBounds(client,*object,object->getAABBWS());
	if(name=="get_text_object_info")
	{
		const std::string mesh_font = convertedTextMeshFontName(toStdString(object->model_url));
		QJsonObject result{{"uid",uid_text},{"selected",client.selected_ob==object},
		{"is_text",object->object_type==WorldObject::ObjectType_Text},{"object_type",(int)object->object_type},
		{"content",QString::fromStdString(object->content)},{"font",QString::fromStdString(mesh_font.empty() ? object->text_font : mesh_font)},
		{"model_url",QString::fromStdString(toStdString(object->model_url))},{"can_edit",can_edit}};
		if(object->object_type==WorldObject::ObjectType_Text)
		{
			// All calls execute on the GUI thread; cache only immutable font coverage.
			static FontCoverage coverage;
			const QVector<uint> text=QString::fromStdString(object->content).toUcs4();
			QJsonObject details{{"font_available",false},{"supports_all_characters",false}};
			try
			{
				const std::string path=client.resolvedWorldTextFontPath(*object);
				const auto& entry=coverage.get(path);
				QJsonArray missing;
				std::set<uint> seen;
				for(uint cp:text) if(!FontCoverage::layoutControl(cp) && !entry.characters.count(cp) && seen.insert(cp).second)
					missing.push_back(QString::fromUcs4(&cp,1));
				details["font_available"]=entry.loaded;
				details["supports_all_characters"]=entry.loaded && missing.isEmpty();
				details["missing_characters"]=missing;
			}
			catch(const glare::Exception&) {}
			if(!details.value("supports_all_characters").toBool())
			{
				try
				{
					WorldObject default_object; default_object.text_font="Default";
					if(coverage.supports(client.resolvedWorldTextFontPath(default_object),text)) details["compatible_font"]="Default";
				}
				catch(const glare::Exception&) {}
				if(!details.contains("compatible_font"))
				{
					QDir directory(QString::fromStdString(client.resources_dir_path+"/fonts"));
					for(const auto& file:directory.entryInfoList({"*.ttf","*.otf","*.woff","*.fon"},QDir::Files,QDir::Name))
						if(coverage.supports(file.absoluteFilePath().toStdString(),text)) { details["compatible_font"]=file.baseName(); break; }
				}
			}
			result["font_coverage"]=details;
		}
		return result;
	}
	if(object->object_type!=WorldObject::ObjectType_Text) fail("The target must be a Text object. It may already have been converted; inspect it before retrying.");
	if(!can_edit) fail("You do not have edit permissions for this text object.");
	if(client.selected_ob==object && (client.selected_ob_picked_up||client.grabbed_axis!=-1)) fail("Finish moving the selected text before converting it.");
	const double z_scale=std::abs(object->scale.z);
	if(!std::isfinite(z_scale)||z_scale<1.0e-6) fail("Text has an invalid depth scale.");
	const std::string font_path=client.resolvedWorldTextFontPath(*object);
	WorldObject default_font_object; default_font_object.text_font="Default";
	std::string fallback_font_path=client.resolvedWorldTextFontPath(default_font_object);
	static FontCoverage coverage;
	const QVector<uint> text_codepoints=QString::fromStdString(object->content).toUcs4();
	if(!coverage.supports(fallback_font_path,text_codepoints))
	{
		const QDir font_dir(QString::fromStdString(client.resources_dir_path+"/fonts"));
		for(const QFileInfo& entry:font_dir.entryInfoList({"*.ttf","*.otf","*.woff","*.fon"},QDir::Files,QDir::Name))
			if(coverage.supports(entry.absoluteFilePath().toStdString(),text_codepoints)) { fallback_font_path=entry.absoluteFilePath().toStdString(); break; }
	}
	const bool font_supports_text=coverage.supports(font_path,text_codepoints);
	BatchedMeshRef mesh=build(object->content,font_path,fallback_font_path,(float)(depth/z_scale));
	WorldObject proposed; proposed.copyNetworkStateFrom(*object); proposed.uid=object->uid; proposed.setAABBOS(mesh->aabb_os);
	if(!canEditBounds(client,*object,proposed.getAABBWS())) fail("The extruded text would extend outside the parcel where you can edit.");
	WorldMaterialRef front_material = object->materials.empty() || object->materials[0].isNull() ? WorldMaterialRef(new WorldMaterial()) : object->materials[0];
	WorldMaterialRef side_material = front_material->clone();
	side_material->name = "Text sides";
	side_material->colour_rgb = Colour3f(side_material->colour_rgb.r * 0.55f, side_material->colour_rgb.g * 0.55f, side_material->colour_rgb.b * 0.55f);
	QTemporaryDir temp; if(!temp.isValid()) fail("Could not create a temporary directory for text mesh.");
	const std::string path=temp.filePath(QString::fromStdString(meshResourceFilename(object->text_font))).toStdString();
	mesh->writeToFile(path);
	const URLString model_url=client.resource_manager->copyLocalFileToResourceDirAndReturnURL(path);
	client.enqueueResourceUpload(model_url);
	client.undo_buffer.startWorldObjectEdit(*object);
	client.removeAndDeleteGLAndPhysicsObjectsForOb(*object);
	object->object_type=WorldObject::ObjectType_Generic;
	object->model_url=model_url; object->lightmap_url.clear(); object->max_model_lod_level=0;
	if(object->materials.empty()) object->materials.push_back(front_material);
	else if(object->materials[0].isNull()) object->materials[0] = front_material;
	object->materials.push_back(side_material);
	object->setAABBOS(mesh->aabb_os);
	object->from_local_other_dirty=true;
	client.world_state->dirty_from_local_objects.insert(object);
	client.undo_buffer.finishWorldObjectEdit(*object);
	client.force_new_undo_edit=true; // Keep the next property edit separate from this type conversion.
	client.loadModelForObject(object.ptr(),lock);
	if(client.selected_ob==object) client.ui_interface->setObjectEditorFromOb(*object,0,client.connectedToUsersWorldOrGodUser());
	return QJsonObject{{"status","queued"},{"uid",QString::number(uid)},{"model_url",QString::fromStdString(toStdString(model_url))},
		{"object_type","Generic"},{"depth_m",depth},{"triangles",(double)(mesh->numIndices()/3)},
		{"fallback_glyphs_used",!font_supports_text},
		{"verification","Resource upload and authoritative object update queued. Confirm with get_object and render_view."}};
}
}
