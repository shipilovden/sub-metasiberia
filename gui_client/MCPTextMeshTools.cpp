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
	std::vector<Vertex> vertices;
	QElapsedTimer timer;
	MeshBuilder() { timer.start(); }
	void checkTime() const { if(timer.elapsed()>2000) fail("Text conversion exceeded its 2 second geometry budget. Shorten the text or use a simpler font."); }
	void triangle(const Vec3f& a, const Vec3f& b, const Vec3f& c)
	{
		const Vec3f cross = crossProduct(b - a, c - a);
		if(cross.length2() < 1.0e-18f) return;
		if(vertices.size() >= 600000) fail("Text mesh exceeds 200000 triangles. Shorten the text or choose a simpler font.");
		const Vec3f n = normalise(cross);
		for(const Vec3f& p : {a,b,c}) vertices.push_back({p.x,p.y,p.z,n.x,n.y,n.z,p.x,p.y});
	}
	void cap(const std::vector<QPointF>& boundary, float depth)
	{
		QPointF middle;
		for(const auto& p : boundary) middle += p;
		middle /= boundary.size();
		for(size_t i=0;i<boundary.size();++i)
		{
			const auto& a=boundary[i]; const auto& b=boundary[(i+1)%boundary.size()];
			triangle(Vec3f((float)middle.x(),(float)middle.y(),depth), Vec3f((float)a.x(),(float)a.y(),depth), Vec3f((float)b.x(),(float)b.y(),depth));
			triangle(Vec3f((float)middle.x(),(float)middle.y(),0), Vec3f((float)b.x(),(float)b.y(),0), Vec3f((float)a.x(),(float)a.y(),0));
		}
	}
};

// Horizontal decomposition supports holes and disconnected contours. Each cap
// boundary is split at every contour intersection on that row, so T junctions
// between neighbouring slabs do not leave open seams after position welding.
void extrudeGlyph(const QPainterPath& input, const QPointF& origin, float depth, MeshBuilder& mesh)
{
	const QPainterPath path = input.simplified();
	const auto polygons = path.toSubpathPolygons(QTransform::fromScale(4,4));
	std::vector<Edge> edges;
	std::vector<double> levels;
	for(const auto& polygon : polygons)
		for(int i=0;i<polygon.size()-1;++i)
		{
			const QPointF a=polygon[i]/168.0+origin, b=polygon[i+1]/168.0+origin;
			if(a==b) continue;
			edges.push_back({a,b}); levels.push_back(a.y()); levels.push_back(b.y());
		}
	if(edges.size()>2048) fail("A glyph outline is too complex for bounded text conversion.");
	std::sort(levels.begin(),levels.end());
	levels.erase(std::unique(levels.begin(),levels.end()),levels.end());
	auto rowPoints = [&](double y) {
		std::vector<double> xs;
		for(const auto& e : edges)
			if(y>=std::min(e.a.y(),e.b.y()) && y<=std::max(e.a.y(),e.b.y()))
			{
				if(e.a.y()==e.b.y()) { xs.push_back(e.a.x()); xs.push_back(e.b.x()); }
				else xs.push_back(e.xAt(y));
			}
		std::sort(xs.begin(),xs.end()); xs.erase(std::unique(xs.begin(),xs.end()),xs.end()); return xs;
	};
	for(size_t row=1;row<levels.size();++row)
	{
		mesh.checkTime();
		const double lo=levels[row-1], hi=levels[row], mid=(lo+hi)*0.5;
		std::vector<const Edge*> active;
		for(const auto& e:edges) if(mid>std::min(e.a.y(),e.b.y()) && mid<std::max(e.a.y(),e.b.y())) active.push_back(&e);
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
		if(e.a.y()!=e.b.y()) for(double y:levels) { const double t=(y-e.a.y())/(e.b.y()-e.a.y()); if(t>0&&t<1) cuts.push_back(t); }
		else for(double x:rowPoints(e.a.y())) { const double t=(x-e.a.x())/(e.b.x()-e.a.x()); if(t>0&&t<1) cuts.push_back(t); }
		std::sort(cuts.begin(),cuts.end());
		const QPointF d=e.b-e.a, left(-d.y(),d.x());
		const double len=std::hypot(left.x(),left.y());
		const QPointF test=((e.a+e.b)*0.5+left*(1.0e-5/len)-origin)*42.0;
		const bool inside_left=path.contains(test);
		for(size_t i=1;i<cuts.size();++i)
		{
			QPointF a=e.a+d*cuts[i-1], b=e.a+d*cuts[i];
			if(!inside_left) std::swap(a,b);
			const Vec3f p((float)a.x(),(float)a.y(),0), q((float)b.x(),(float)b.y(),0), dz(0,0,depth);
			mesh.triangle(p,q,q+dz); mesh.triangle(p,q+dz,p+dz);
		}
	}
}

BatchedMeshRef build(const std::string& text, const std::string& font_path, float depth)
{
	if(text.empty() || UTF8Utils::numCodePointsInString(text)>256) fail("Text conversion requires 1..256 Unicode characters.");
	TextRendererRef renderer = new TextRenderer();
	TextRendererFontFaceRef font = new TextRendererFontFace(renderer,font_path,42);
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
		const FT_UInt glyph=FT_Get_Char_Index(font->face,cp);
		if(!glyph) fail("The selected font has no outline for a character in this text. Choose a font supporting every character.");
		if(FT_Load_Glyph(font->face,glyph,FT_LOAD_DEFAULT|FT_LOAD_NO_BITMAP)) fail("Could not load the glyph outline.");
		if(font->face->glyph->format!=FT_GLYPH_FORMAT_OUTLINE) fail("Bitmap/colour-only glyphs cannot be converted to an outline mesh.");
		outline_points+=font->face->glyph->outline.n_points;
		if(font->face->glyph->outline.n_points>4096 || outline_points>32768) fail("Text outlines exceed the conversion complexity budget.");
		builder.checkTime();
		const double advance=font->face->glyph->advance.x/(64.0*42.0);
		OutlineBuilder outline; outline.path.setFillRule(Qt::WindingFill);
		FT_Outline_Funcs funcs{OutlineBuilder::move,OutlineBuilder::line,OutlineBuilder::quadratic,OutlineBuilder::cubic,0,0};
		if(FT_Outline_Decompose(&font->face->glyph->outline,&funcs,&outline)) fail("Could not extract the glyph outline.");
		outline.path.closeSubpath();
		if(!outline.path.isEmpty()) extrudeGlyph(outline.path,pen,depth,builder);
		pen.rx()+=advance;
	}
	if(builder.vertices.empty()) fail("Text contains no visible outline geometry.");
	BatchedMeshRef mesh = new BatchedMesh();
	mesh->vert_attributes.push_back({BatchedMesh::VertAttribute_Position,BatchedMesh::ComponentType_Float,0});
	mesh->vert_attributes.push_back({BatchedMesh::VertAttribute_Normal,BatchedMesh::ComponentType_Float,12});
	mesh->vert_attributes.push_back({BatchedMesh::VertAttribute_UV_0,BatchedMesh::ComponentType_Float,24});
	mesh->vertex_data.resize(builder.vertices.size()*sizeof(Vertex));
	std::memcpy(mesh->vertex_data.data(),builder.vertices.data(),mesh->vertex_data.size());
	js::Vector<uint32,16> indices(builder.vertices.size());
	mesh->aabb_os=js::AABBox::emptyAABBox();
	for(size_t i=0;i<builder.vertices.size();++i) {
		indices[i]=(uint32)i; const auto& v=builder.vertices[i]; mesh->aabb_os.enlargeToHoldPoint(Vec4f(v.x,v.y,v.z,1));
	}
	mesh->setIndexDataFromIndices(indices,builder.vertices.size());
	mesh->batches.push_back({0,(uint32)indices.size(),0});
	mesh->checkValidAndSanitiseMesh();
	return mesh;
}
}

bool handles(const QString& name) { return name=="convert_text_to_mesh" || name=="get_text_object_info"; }
QJsonArray schemas()
{
	return QJsonDocument::fromJson(R"json([{
"name":"get_text_object_info",
"description":"Read a loaded Text object's exact UID, current text, chosen font, font coverage and edit permission from the native client. Omit uid to inspect the currently selected object, for requests such as 'make this text 3D'. Read-only. Returns the current object type even after conversion. font_coverage reports missing_characters and an optional compatible_font containing every current character; if unsupported, explain the style change and set a compatible font before conversion rather than losing letters. Coverage is not a guarantee that bitmap/emoji glyphs have extrudable outlines. Use its exact uid in convert_text_to_mesh; do not guess the selected object from a screenshot.",
"inputSchema":{"type":"object","properties":{"uid":{"type":"string","pattern":"^[0-9]+$"}},"additionalProperties":false}
},{
"name":"convert_text_to_mesh",
"description":"Convert an existing world Text object's real font outlines into an editable solid native .bmesh, replacing that same UID. Read the object first. Preserves text content, font name, position, rotation, scale, materials and scripts; text is no longer editable as letters after conversion (Undo restores Text). Extrudes along local +Z from the existing text plane; depth_m is world thickness accounting for local Z scale. Supports holes, Cyrillic and multiline text when the font contains outline glyphs. Maximum 256 characters, no bevel or bitmap/color emoji. Uses the connected Qt client's exact bundled font and native resource upload/update path with edit/placement permission checks. Returns queued, not server confirmation: use get_object and render_view to confirm Generic type/model_url and visible result before claiming completion. Never invent a successful conversion from this queue acknowledgement alone.",
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
		QJsonObject result{{"uid",uid_text},{"selected",client.selected_ob==object},
		{"is_text",object->object_type==WorldObject::ObjectType_Text},{"object_type",(int)object->object_type},
		{"content",QString::fromStdString(object->content)},{"font",QString::fromStdString(object->text_font)},
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
	BatchedMeshRef mesh=build(object->content,font_path,(float)(depth/z_scale));
	WorldObject proposed; proposed.copyNetworkStateFrom(*object); proposed.uid=object->uid; proposed.setAABBOS(mesh->aabb_os);
	if(!canEditBounds(client,*object,proposed.getAABBWS())) fail("The extruded text would extend outside the parcel where you can edit.");
	QTemporaryDir temp; if(!temp.isValid()) fail("Could not create a temporary directory for text mesh.");
	const std::string path=temp.filePath("text_mesh.bmesh").toStdString();
	mesh->writeToFile(path);
	const URLString model_url=client.resource_manager->copyLocalFileToResourceDirAndReturnURL(path);
	client.enqueueResourceUpload(model_url);
	client.undo_buffer.startWorldObjectEdit(*object);
	client.removeAndDeleteGLAndPhysicsObjectsForOb(*object);
	object->object_type=WorldObject::ObjectType_Generic;
	object->model_url=model_url; object->lightmap_url.clear(); object->max_model_lod_level=0;
	object->setAABBOS(mesh->aabb_os);
	object->from_local_other_dirty=true;
	client.world_state->dirty_from_local_objects.insert(object);
	client.undo_buffer.finishWorldObjectEdit(*object);
	client.force_new_undo_edit=true; // Keep the next property edit separate from this type conversion.
	client.loadModelForObject(object.ptr(),lock);
	if(client.selected_ob==object) client.ui_interface->setObjectEditorFromOb(*object,0,client.connectedToUsersWorldOrGodUser());
	return QJsonObject{{"status","queued"},{"uid",QString::number(uid)},{"model_url",QString::fromStdString(toStdString(model_url))},
		{"object_type","Generic"},{"depth_m",depth},{"triangles",(double)(mesh->numIndices()/3)},
		{"verification","Resource upload and authoritative object update queued. Confirm with get_object and render_view."}};
}
}
