// Private implementation of the native MCP geometry tools. Included only by MCPHandlers.cpp.
#pragma once

#include <graphics/BatchedMesh.h>
#include <dll/include/IndigoMesh.h>
#include <indigo/UVUnwrapper.h>
#include <maths/Matrix4f.h>
#include <utils/StandardPrintOutput.h>
#include <utils/FileUtils.h>
#include <utils/FileChecksum.h>
#include <utils/Base64.h>
#include <graphics/PNGDecoder.h>
#include <graphics/Map2D.h>
#include <cmath>
#include <map>
#include <array>
#include "MCPPrimitiveMeshes.h"
#include "MCPMeshOperations.h"

namespace MCPHandlers { namespace Modeling {

static URLString textureURL(const JSONParser& p, const JSONNode& n, const char* key)
{
	const std::string s = n.getChildStringValueWithDefaultVal(p,key,"");
	bool safe_resource_path=!s.empty() && s[0]!='/' && s.find_first_of("\\:?#%") == std::string::npos && s.find("//") == std::string::npos;
	for(size_t begin=0;safe_resource_path && begin<s.size();) {
		const size_t end=s.find('/',begin), segment_end=end==std::string::npos?s.size():end;
		const std::string segment=s.substr(begin,segment_end-begin);
		if(segment.empty() || segment=="." || segment=="..") { safe_resource_path=false;break; }
		for(unsigned char c:segment) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')) { safe_resource_path=false;break; }
		if(end==std::string::npos) break;
		begin=end+1;
	}
	if(!s.empty() && (s.size()>512 || !safe_resource_path ||
		!(hasExtension(s,"png") || hasExtension(s,"jpg") || hasExtension(s,"jpeg") || hasExtension(s,"webp") || hasExtension(s,"basis") || hasExtension(s,"ktx2") || hasExtension(s,"exr"))))
		throw glare::Exception("Use an existing image resource URL or one returned by import_texture, not an external URL or file path.");
	return toURLString(s);
}

static void validateTextures(ServerAllWorldsState& all, const WorldObject& ob)
{
	for(const auto& m : ob.materials)
		for(const auto& u : {m->colour_texture_url, m->roughness.texture_url, m->normal_map_url, m->emission_texture_url})
			if(!u.empty() && !all.resource_manager->isFileForURLPresent(u)) throw glare::Exception("Texture resource is not available on the server: " + toStdString(u));
}

static std::string importTexture(ServerAllWorldsState& all, const JSONParser& p, const JSONNode& n)
{
	if(all.isInReadOnlyMode()) throw glare::Exception("Server is in read-only mode.");
	const std::string& encoded=n.getChildStringValue(p,"png_base64");
	if(encoded.size()>6*1024*1024) throw glare::Exception("PNG upload exceeds 6 MiB base64.");
	std::vector<unsigned char> bytes; Base64::decode(encoded,bytes);
	const unsigned char signature[]={137,80,78,71,13,10,26,10};
	if(bytes.size()<33 || !std::equal(signature,signature+8,bytes.begin())) throw glare::Exception("Expected a PNG image.");
	auto be32=[&](size_t o) { return ((uint32)bytes[o]<<24)|((uint32)bytes[o+1]<<16)|((uint32)bytes[o+2]<<8)|bytes[o+3]; };
	const uint32 width=be32(16), height=be32(20);
	if(!width || !height || width>2048 || height>2048) throw glare::Exception("Image dimensions must be 1..2048 pixels.");
	Reference<Map2D> decoded=PNGDecoder::decodeFromBuffer(bytes.data(),bytes.size());
	const std::string tmp=FileUtils::writeEntireFileToTempFile((const char*)bytes.data(),bytes.size());
	URLString url;
	try {
		url=ResourceManager::URLForNameAndExtensionAndHash("mcp_texture","png",FileChecksum::fileChecksum(tmp));
		all.resource_manager->copyLocalFileToResourceDir(tmp,url); FileUtils::deleteFile(tmp);
	} catch(...) { try { FileUtils::deleteFile(tmp); } catch(...) {} throw; }
	return "{\"texture_url\":\""+web::Escaping::JSONEscape(toStdString(url))+"\",\"width\":"+toString(width)+",\"height\":"+toString(height)+"}";
}

static double number(const JSONParser& p, const JSONNode& n, const std::string& key, double fallback, double lo, double hi)
{
	const double v = n.getChildDoubleValueWithDefaultVal(p, key, fallback);
	if(!std::isfinite(v) || v < lo || v > hi) throw glare::Exception(key + " is outside its allowed range.");
	return v;
}

static int integer(const JSONParser& p, const JSONNode& n, const std::string& key, int fallback, int lo, int hi)
{
	const double v = number(p, n, key, fallback, lo, hi);
	if(v != std::floor(v)) throw glare::Exception(key + " must be an integer.");
	return (int)v;
}

static Vec3d vector(const JSONParser& p, const JSONNode& n, const std::string& key, const Vec3d& fallback, double lo = -10000, double hi = 10000)
{
	if(!n.hasChild(key)) return fallback;
	double v[3];
	n.getChildArray(p, key).parseDoubleArrayValues(p, 3, v);
	for(int i=0; i<3; ++i) if(!std::isfinite(v[i]) || v[i] < lo || v[i] > hi) throw glare::Exception(key + " components are outside their allowed range.");
	return Vec3d(v[0], v[1], v[2]);
}

static Vec3d position(const JSONParser& p, const JSONNode& n, const Vec3d& fallback)
{
	if(!n.hasChild("pos")) return fallback;
	const JSONNode& v = n.getChildObject(p, "pos");
	if(!v.hasChild("x") || !v.hasChild("y") || !v.hasChild("z")) throw glare::Exception("pos requires x, y and z.");
	return Vec3d(number(p,v,"x",0,-1e7,1e7), number(p,v,"y",0,-1e7,1e7), number(p,v,"z",0,-1e7,1e7));
}

static std::string vecJSON(const Vec3d& v)
{
	return "[" + doubleToString(v.x) + "," + doubleToString(v.y) + "," + doubleToString(v.z) + "]";
}

static std::string describe(const WorldObject& ob)
{
	const js::AABBox box = ob.getAABBWS();
	std::string out = "{\"uid\":" + toString(ob.uid.value()) + ",\"type\":\"" + WorldObject::objectTypeString(ob.object_type) +
		"\",\"model_url\":\"" + web::Escaping::JSONEscape(toStdString(ob.model_url)) + "\",\"pos\":" + vecJSON(ob.pos) +
		",\"scale\":" + vecJSON(Vec3d(ob.scale.x,ob.scale.y,ob.scale.z)) + ",\"angle\":" + doubleToString(ob.angle) +
		",\"axis\":" + vecJSON(Vec3d(ob.axis.x,ob.axis.y,ob.axis.z)) + ",\"bounds_min\":" + vecJSON(Vec3d(box.min_[0],box.min_[1],box.min_[2])) +
		",\"bounds_max\":" + vecJSON(Vec3d(box.max_[0],box.max_[1],box.max_[2])) + ",\"content\":\"" + web::Escaping::JSONEscape(ob.content) + "\",\"materials\":[";
	for(size_t i=0; i<ob.materials.size(); ++i)
	{
		if(i) out += ",";
		const WorldMaterial& m = *ob.materials[i];
		out += "{\"color\":" + vecJSON(Vec3d(m.colour_rgb.r,m.colour_rgb.g,m.colour_rgb.b)) + ",\"roughness\":" + doubleToString(m.roughness.val) +
			",\"metallic\":" + doubleToString(m.metallic_fraction.val) + ",\"opacity\":" + doubleToString(m.opacity.val) +
			",\"emission\":"+vecJSON(Vec3d(m.emission_rgb.r,m.emission_rgb.g,m.emission_rgb.b))+",\"emission_strength\":"+doubleToString(m.emission_lum_flux_or_lum)+
			",\"color_texture\":\""+web::Escaping::JSONEscape(toStdString(m.colour_texture_url))+"\",\"metallic_roughness_texture\":\""+web::Escaping::JSONEscape(toStdString(m.roughness.texture_url))+
			"\",\"normal_texture\":\""+web::Escaping::JSONEscape(toStdString(m.normal_map_url))+"\",\"emission_texture\":\""+web::Escaping::JSONEscape(toStdString(m.emission_texture_url))+
			"\",\"uv_scale\":["+doubleToString(m.tex_matrix.e[0])+","+doubleToString(m.tex_matrix.e[3])+"],\"double_sided\":"+std::string((m.flags & WorldMaterial::DOUBLE_SIDED_FLAG)?"true":"false")+"}";
	}
	return out + "]}";
}

static void materials(const JSONParser& p, const JSONNode& n, WorldObject& ob)
{
	if(!n.hasChild("materials")) { if(ob.materials.empty()) ob.materials.push_back(new WorldMaterial()); return; }
	const JSONNode& a = n.getChildArray(p,"materials");
	if(a.child_indices.empty() || a.child_indices.size()>255) throw glare::Exception("Use 1..255 materials.");
	ob.materials.clear();
	for(uint32 index : a.child_indices)
	{
		const JSONNode& v = p.nodes[index];
		WorldMaterialRef m = new WorldMaterial();
		const Vec3d c = vector(p,v,"color",Vec3d(0.65,0.65,0.65),0,1);
		m->colour_rgb = Colour3f((float)c.x,(float)c.y,(float)c.z);
		m->roughness.val = (float)number(p,v,"roughness",0.5,0,1);
		m->metallic_fraction.val = (float)number(p,v,"metallic",0,0,1);
		m->opacity.val = (float)number(p,v,"opacity",1,0,1);
		const Vec3d e = vector(p,v,"emission",Vec3d(0,0,0),0,1);
		m->emission_rgb = Colour3f((float)e.x,(float)e.y,(float)e.z);
		m->emission_lum_flux_or_lum = (float)number(p,v,"emission_strength",0,0,10000);
		m->colour_texture_url=textureURL(p,v,"color_texture");
		m->roughness.texture_url=textureURL(p,v,"metallic_roughness_texture");
		m->normal_map_url=textureURL(p,v,"normal_texture");
		m->emission_texture_url=textureURL(p,v,"emission_texture");
		double uv[2]={1,1}; if(v.hasChild("uv_scale")) v.getChildArray(p,"uv_scale").parseDoubleArrayValues(p,2,uv);
		for(double x:uv) if(!std::isfinite(x) || std::abs(x)<.0001 || std::abs(x)>10000) throw glare::Exception("Invalid UV scale.");
		m->tex_matrix=Matrix2f((float)uv[0],0,0,(float)uv[1]);
		if(v.getChildBoolValueWithDefaultVal(p,"double_sided",false)) m->flags |= WorldMaterial::DOUBLE_SIDED_FLAG;
		if(!m->colour_texture_url.empty()) m->flags |= WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG;
		ob.materials.push_back(m);
	}
}

static bool editable(const WorldObject& ob, const ServerWorldState& w, const UserID& u)
{
	return isGodUser(u) || w.details.owner_id == u || ob.creator_id == u;
}

static bool canPlace(const js::AABBox& bounds, ServerWorldState& w, WorldStateLock& lock, const UserID& u)
{
	if(!u.valid()) return false;
	if(isGodUser(u) || w.details.owner_id == u) return true;
	// A writable parcel never grants permission to overlap a different, non-writable parcel.
	for(const auto& entry : w.getParcels(lock))
		if(!entry.second->userHasWritePerms(u) && entry.second->AABBIntersectsParcel(bounds)) return false;
	for(const auto& entry : w.getParcels(lock))
		if(entry.second->userHasWritePerms(u) && Parcel::AABBInParcelBounds(bounds,entry.second->aabb_min,entry.second->aabb_max)) return true;
	return false;
}

static const char* buildPermissionMessage()
{
	return "You do not have permission to build at this location. Build entirely inside your own or an explicitly writable parcel, an authorised sandbox area, or your personal world. The whole object, not just its origin, must fit. Do not claim success or move the build elsewhere without the user's agreement.";
}

static void permission(const WorldObject& ob, ServerWorldState& w, WorldStateLock& lock, const UserID& u)
{
	if(!canPlace(ob.getAABBWS(),w,lock,u)) throw glare::Exception(std::string("BUILD_PERMISSION_DENIED: ")+buildPermissionMessage());
}

static std::string checkBuildPermissions(ServerAllWorldsState& all, ServerWorldState& w, const JSONParser& p, const JSONNode& n, const UserID& user)
{
	if(!n.hasChild("bounds_min") || !n.hasChild("bounds_max")) throw glare::Exception("Specify the planned complete world-space bounds_min and bounds_max.");
	const Vec3d lo=vector(p,n,"bounds_min",Vec3d(0,0,0),-1e9,1e9),hi=vector(p,n,"bounds_max",Vec3d(0,0,0),-1e9,1e9);
	if(lo.x>=hi.x || lo.y>=hi.y || lo.z>=hi.z) throw glare::Exception("Build bounds must have positive extent on every axis.");
	const js::AABBox bounds(lo.toVec4fPoint(),hi.toVec4fPoint());
	WorldStateLock lock(all.mutex);
	const bool read_only=all.isInReadOnlyMode();
	const bool allowed=!read_only && canPlace(bounds,w,lock,user);
	std::string result="{\"allowed\":"+std::string(allowed?"true":"false")+",\"reason\":\""+(allowed?"allowed":read_only?"server_read_only":"BUILD_PERMISSION_DENIED")+"\",\"message\":\""+
		web::Escaping::JSONEscape(allowed?"Placement is permitted now; the write operation will check again.":read_only?"Server is currently read-only.":buildPermissionMessage())+"\",\"can_build_anywhere_in_this_world\":"+
		std::string(!read_only && (isGodUser(user) || w.details.owner_id==user)?"true":"false")+",\"writable_parcels\":[";
	// Suggestions are informational only. Never automatically relocate a build.
	int count=0;
	for(const auto& entry:w.getParcels(lock)) if(entry.second->userHasWritePerms(user))
	{
		if(count++) result+=",";
		result+="{\"bounds_min\":"+vecJSON(entry.second->aabb_min)+",\"bounds_max\":"+vecJSON(entry.second->aabb_max)+"}";
		if(count>=20) break;
	}
	return result+"]}";
}

static void dirtyChunk(ServerWorldState& w, WorldStateLock& lock, const WorldObject& ob)
{
	const Vec4f c = ob.getCentroidWS();
	const auto it = w.getLODChunks(lock).find(Vec3i((int)std::floor(c[0]/128.f),(int)std::floor(c[1]/128.f),0));
	if(it != w.getLODChunks(lock).end()) it->second->needs_rebuild = true;
}

static Vec3d nearAvatar(ServerWorldState& w, WorldStateLock& lock, const std::string& username)
{
	for(const auto& a : w.getAvatars(lock))
		if(a.second->name == username)
			return a.second->pos + Vec3d(3 * std::cos(a.second->rotation.z),3 * std::sin(a.second->rotation.z),0);
	throw glare::Exception("Your avatar is not present in this world; specify pos explicitly or reconnect.");
}

static void transform(const JSONParser& p, const JSONNode& n, WorldObject& ob)
{
	ob.pos = position(p,n,ob.pos);
	Vec3d scale = vector(p,n,"scale",Vec3d(ob.scale.x,ob.scale.y,ob.scale.z),0.001,100);
	// Compatibility with the original tools.
	scale.x = number(p,n,"size_x",scale.x,0.001,100);
	scale.y = number(p,n,"size_y",scale.y,0.001,100);
	scale.z = number(p,n,"size_z",scale.z,0.001,100);
	scale.x = number(p,n,"scale_x",scale.x,0.001,100);
	scale.y = number(p,n,"scale_y",scale.y,0.001,100);
	scale.z = number(p,n,"scale_z",scale.z,0.001,100);
	ob.scale = Vec3f((float)scale.x,(float)scale.y,(float)scale.z);
	const Vec3d axis = vector(p,n,"axis",Vec3d(ob.axis.x,ob.axis.y,ob.axis.z),-1,1);
	const double len = std::sqrt(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z);
	if(len < 1.e-6) throw glare::Exception("Rotation axis must be nonzero.");
	ob.axis = Vec3f((float)(axis.x/len),(float)(axis.y/len),(float)(axis.z/len));
	ob.angle = (float)number(p,n,"angle",ob.angle,-1000,1000);
	if(n.hasChild("content")) ob.content = n.getChildStringValue(p,"content");
	if(ob.content.size()>WorldObject::MAX_CONTENT_SIZE) throw glare::Exception("Object description is too long.");
	ob.transformChanged();
}

// CPU geometry and resource I/O happen outside the global world lock. Only publication is locked.
static std::string publish(ServerAllWorldsState& all, ServerWorldState& w, WorldObjectRef ob, const JSONParser& p, const JSONNode& n,
	const UserID& user, const std::string& username, const Reference<BatchedMesh>& mesh, const std::vector<WorldObjectRef>* sources = nullptr)
{
	if(all.isInReadOnlyMode()) throw glare::Exception("Server is in read-only mode.");
	const bool replacing = n.hasChild("replace_uid");
	const char* consume_key=n.hasChild("consume_uid")?"consume_uid":"operand_uid";
	const bool consuming = n.hasChild("consume_uid") || n.hasChild("operand_uid");
	if(consuming && !replacing) throw glare::Exception("consume_uid is supported only when replacing an existing mesh.");
	validateTextures(all,*ob);
	const UID uid((uint64)number(p,n,"replace_uid",0,0,9007199254740991.0));
	if(replacing && number(p,n,"replace_uid",0,0,9007199254740991.0)!=(double)uid.value()) throw glare::Exception("replace_uid must be an integer.");
	const uint64 consume_raw=consuming?(uint64)number(p,n,consume_key,0,0,9007199254740991.0):0;
	if(consuming && ((double)consume_raw!=number(p,n,consume_key,0,0,9007199254740991.0) || UID(consume_raw)==uid)) throw glare::Exception("The consumed operand must be a different integer object UID.");
	WorldObjectRef expected;
	Vec3d expected_pos(0,0,0);
	URLString expected_url;
	TimeStamp expected_modified;
	const auto validate_sources=[&](WorldStateLock& lock) {
		if(!sources) return;
		for(const auto& source:*sources) {
			const auto it=w.getObjects(lock).find(source->uid);
			if(it==w.getObjects(lock).end() || it->second->state==WorldObject::State_Dead) throw glare::Exception("A source object was removed during mesh editing.");
			const WorldObject& current=*it->second;
			bool same=current.model_url==source->model_url && current.object_type==source->object_type && current.last_modified_time.time==source->last_modified_time.time &&
				current.pos==source->pos && current.scale==source->scale && current.axis==source->axis && current.angle==source->angle && current.materials.size()==source->materials.size();
			for(size_t i=0;same && i<current.materials.size();++i) same=(*current.materials[i]==*source->materials[i]);
			if(!same || !editable(current,w,user)) throw glare::Exception("A source object changed during mesh editing. Read it again and retry.");
		}
	};
	{
		WorldStateLock lock(all.mutex);
		validate_sources(lock);
		if(replacing)
		{
			const auto it=w.getObjects(lock).find(uid);
			if(it==w.getObjects(lock).end() || it->second->state==WorldObject::State_Dead) throw glare::Exception("Replacement target does not exist.");
			expected=it->second;
			if(!editable(*expected,w,user)) throw glare::Exception("Cannot replace another user's object.");
			if(expected->object_type!=WorldObject::ObjectType_Generic && expected->object_type!=WorldObject::ObjectType_VoxelGroup)
				throw glare::Exception("Geometry replacement is limited to model and voxel objects; this object needs its specialised editor.");
			expected_pos=expected->pos; expected_url=expected->model_url; expected_modified=expected->last_modified_time;
			ob->pos=expected->pos; ob->scale=expected->scale; ob->axis=expected->axis; ob->angle=expected->angle;
		}
		else if(!n.hasChild("pos")) ob->pos=nearAvatar(w,lock,username);
		transform(p,n,*ob);
		permission(*ob,w,lock,user);
	}
	if(mesh.nonNull())
	{
		const std::string tmp = FileUtils::writeEntireFileToTempFile("",0);
		try
		{
			mesh->writeToFile(tmp);
			ob->model_url = ResourceManager::URLForNameAndExtensionAndHash("mcp_model","bmesh",FileChecksum::fileChecksum(tmp));
			all.resource_manager->copyLocalFileToResourceDir(tmp,ob->model_url);
			FileUtils::deleteFile(tmp);
		}
		catch(...) { try { FileUtils::deleteFile(tmp); } catch(...) {} throw; }
	}
	WorldStateLock lock(all.mutex);
	validate_sources(lock);
	if(all.isInReadOnlyMode()) throw glare::Exception("Server is now in read-only mode.");
	permission(*ob,w,lock,user);
	if(replacing)
	{
		const auto it=w.getObjects(lock).find(uid);
		if(it==w.getObjects(lock).end() || it->second!=expected || expected->state==WorldObject::State_Dead ||
			expected->last_modified_time.time != expected_modified.time || expected->pos != expected_pos || expected->model_url != expected_url)
			throw glare::Exception("The target changed during modeling. Read it again before replacing it.");
		if(!editable(*expected,w,user)) throw glare::Exception("Object permission changed.");
		dirtyChunk(w,lock,*expected);
		// Preserve identity, ownership and physical/script settings; only replace the modeled geometry/materials and transform.
		expected->object_type=ob->object_type; expected->model_url=ob->model_url;
		expected->materials=ob->materials; expected->setCompressedVoxels(ob->getCompressedVoxels());
		expected->pos=ob->pos; expected->scale=ob->scale; expected->axis=ob->axis; expected->angle=ob->angle;
		if(n.hasChild("content")) expected->content=ob->content;
		expected->setAABBOS(ob->getAABBOS()); expected->max_model_lod_level=0;
		expected->lightmap_url=URLString();
		ob=expected;
	}
	else
	{
		ob->creator_id=user; ob->creator_name=username; ob->created_time=TimeStamp::currentTime();
		ob->uid=all.getNextObjectUID(); ob->state=WorldObject::State_JustCreated;
		w.getObjects(lock).insert(std::make_pair(ob->uid,ob));
	}
	if(consuming)
	{
		const auto it=w.getObjects(lock).find(UID(consume_raw));
		if(it==w.getObjects(lock).end() || it->second->state==WorldObject::State_Dead || !editable(*it->second,w,user)) throw glare::Exception("The consumed operand is missing or not editable.");
		dirtyChunk(w,lock,*it->second);it->second->state=WorldObject::State_Dead;it->second->last_modified_time=TimeStamp::currentTime();it->second->from_remote_other_dirty=true;
		w.addWorldObjectAsDBDirty(it->second,lock);w.getDirtyFromRemoteObjects(lock).insert(it->second);dirtyChunk(w,lock,*it->second);
	}
	ob->last_modified_time=TimeStamp::currentTime(); ob->from_remote_other_dirty=true;
	w.addWorldObjectAsDBDirty(ob,lock); w.getDirtyFromRemoteObjects(lock).insert(ob); dirtyChunk(w,lock,*ob); all.markAsChanged();
	return describe(*ob);
}

static MCPMeshOps::Mesh operationsMesh(const Indigo::Mesh& native, size_t max_triangles = 40000);
static std::string localMeshResourcePath(ServerAllWorldsState& all,const URLString& url);

struct MeshBuilder
{
	Indigo::Mesh mesh;
	Vec3d center=Vec3d(0,0,0), size=Vec3d(1,1,1), rotation=Vec3d(0,0,0);
	int material=0;
	Vec3d point(Vec3d v) const
	{
		v=Vec3d(v.x*size.x,v.y*size.y,v.z*size.z);
		const double k=3.141592653589793/180;
		const double x=rotation.x*k,y=rotation.y*k,z=rotation.z*k;
		v=Vec3d(v.x,v.y*std::cos(x)-v.z*std::sin(x),v.y*std::sin(x)+v.z*std::cos(x));
		v=Vec3d(v.x*std::cos(y)+v.z*std::sin(y),v.y,-v.x*std::sin(y)+v.z*std::cos(y));
		return center+Vec3d(v.x*std::cos(z)-v.y*std::sin(z),v.x*std::sin(z)+v.y*std::cos(z),v.z);
	}
	void triangle(Vec3d a, Vec3d b, Vec3d c, Vec3d na=Vec3d(0,0,0), Vec3d nb=Vec3d(0,0,0), Vec3d nc=Vec3d(0,0,0))
	{
		if(mesh.triangles.size()>=100000) throw glare::Exception("Mesh budget exceeded: maximum 100000 triangles.");
		a=point(a); b=point(b); c=point(c);
		const Vec3d u=b-a,v=c-a;
		Vec3d n(u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x);
		const double len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
		if(len<1e-12) return; // Pole/collapsed faces of parametric surfaces.
		n=n/len;
		const uint32 start=(uint32)mesh.vert_positions.size();
		const Vec3d points[]={a,b,c},normals[]={na,nb,nc};
		for(int i=0;i<3;++i)
		{
			Vec3d use_normal=n;
			if(normals[i]!=Vec3d(0,0,0))
			{
				use_normal=point(Vec3d(normals[i].x/(size.x*size.x),normals[i].y/(size.y*size.y),normals[i].z/(size.z*size.z)))-center;
				use_normal=use_normal/std::sqrt(use_normal.x*use_normal.x+use_normal.y*use_normal.y+use_normal.z*use_normal.z);
			}
			mesh.addVertex(Indigo::Vec3f((float)points[i].x,(float)points[i].y,(float)points[i].z),Indigo::Vec3f((float)use_normal.x,(float)use_normal.y,(float)use_normal.z));
		}
		// Object-space dominant-axis projection, one repeat per local metre.
		// All-zero UVs made procedural/PBR textures impossible on MCP meshes.
		const uint32 uv_start=(uint32)mesh.uv_pairs.size();
		for(const Vec3d& q:points)
			mesh.uv_pairs.push_back(std::abs(n.z)>=std::abs(n.x) && std::abs(n.z)>=std::abs(n.y) ? Indigo::Vec2f((float)q.x,(float)q.y) :
				std::abs(n.x)>std::abs(n.y) ? Indigo::Vec2f((float)q.y,(float)q.z) : Indigo::Vec2f((float)q.x,(float)q.z));
		mesh.num_uv_mappings=1;
		const uint32 vi[]={start,start+1,start+2}, uv[]={uv_start,uv_start+1,uv_start+2};
		mesh.addTriangle(vi,uv,(uint32)material);
	}
	void quad(Vec3d a,Vec3d b,Vec3d c,Vec3d d) { triangle(a,b,c); triangle(a,c,d); }
	void setLastTriangleUV(double u0,double v0,double u1,double v1,double u2,double v2)
	{
		if(mesh.triangles.empty()) throw glare::Exception("Cannot assign UVs without a triangle.");
		const double values[]={u0,v0,u1,v1,u2,v2};
		for(double value:values) if(!std::isfinite(value) || std::abs(value)>1e6) throw glare::Exception("Invalid UV coordinate.");
		const auto& tri=mesh.triangles.back();
		for(int i=0;i<3;++i) mesh.uv_pairs[tri.uv_indices[i]]=Indigo::Vec2f((float)values[2*i],(float)values[2*i+1]);
	}
	void appendOperationsMesh(const MCPMeshOps::Mesh& source)
	{
		for(const auto& t:source) {
			Vec3d p[3],n[3]; for(int i=0;i<3;++i) { const auto& v=t.vertices[i];p[i]=Vec3d(v.position.x,v.position.y,v.position.z);n[i]=Vec3d(v.normal.x,v.normal.y,v.normal.z); }
			material=(int)t.material; const size_t before=mesh.triangles.size();
			triangle(p[0],p[1],p[2],n[0],n[1],n[2]);
			if(mesh.triangles.size()!=before+1) throw glare::Exception("Mesh operation produced a collapsed triangle.");
			setLastTriangleUV(t.vertices[0].u,t.vertices[0].v,t.vertices[1].u,t.vertices[1].v,t.vertices[2].u,t.vertices[2].v);
		}
	}
	void boxPart(const JSONParser& p, const JSONNode& part, bool deformed, int material_count)
	{
		Vec3d vertices[]={Vec3d(-.5,-.5,-.5),Vec3d(.5,-.5,-.5),Vec3d(.5,.5,-.5),Vec3d(-.5,.5,-.5),
			Vec3d(-.5,-.5,.5),Vec3d(.5,-.5,.5),Vec3d(.5,.5,.5),Vec3d(-.5,.5,.5)};
		if(deformed)
		{
			const JSONNode& list=part.getChildArray(p,"vertices");
			if(list.child_indices.size()!=8) throw glare::Exception("deformed_box requires exactly 8 vertices in cube corner order.");
			for(int i=0;i<8;++i)
			{
				double v[3];p.nodes[list.child_indices[i]].parseDoubleArrayValues(p,3,v);
				for(int j=0;j<3;++j) if(!std::isfinite(v[j]) || std::abs(v[j])>1000) throw glare::Exception("Invalid deformed box vertex.");
				vertices[i]=Vec3d(v[0],v[1],v[2]);
			}
		}
		double face_mats[6]={(double)material,(double)material,(double)material,(double)material,(double)material,(double)material};
		if(part.hasChild("face_materials")) part.getChildArray(p,"face_materials").parseDoubleArrayValues(p,6,face_mats);
		const int faces[6][4]={{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
		const int saved_material=material;
		for(int i=0;i<6;++i)
		{
			if(!std::isfinite(face_mats[i]) || face_mats[i]!=std::floor(face_mats[i]) || face_mats[i]<0 || face_mats[i]>=material_count) throw glare::Exception("Invalid face material index.");
			material=(int)face_mats[i];
			const size_t before=mesh.triangles.size();
			quad(vertices[faces[i][0]],vertices[faces[i][1]],vertices[faces[i][2]],vertices[faces[i][3]]);
			if(mesh.triangles.size()!=before+2) throw glare::Exception("Cube deformation collapsed a face. Use indexed triangles for topology changes.");
		}
		material=saved_material;
	}
	void profile(const JSONParser& p, const JSONNode& part, bool lathe, int segments)
	{
		const JSONNode& list=part.getChildArray(p,"profile");
		if(list.child_indices.size()<(lathe?2:3) || list.child_indices.size()>128) throw glare::Exception("Profile needs 2 (lathe) or 3 (extrude) to 128 points.");
		std::vector<Vec3d> points;
		for(uint32 i:list.child_indices)
		{
			double v[2]; p.nodes[i].parseDoubleArrayValues(p,2,v);
			if(!std::isfinite(v[0]) || !std::isfinite(v[1]) || std::abs(v[0])>1000 || std::abs(v[1])>1000 || (lathe && v[0]<0)) throw glare::Exception("Invalid profile coordinate/radius.");
			points.push_back(Vec3d(v[0],v[1],0));
		}
		if(lathe)
		{
			// Ordered radius/Z profile: bottom to top on the outside, optionally down the inside.
			for(size_t j=0;j+1<points.size();++j)
			{
				const double dr=points[j+1].x-points[j].x,dz=points[j+1].y-points[j].y;
				if(dr*dr+dz*dz<1e-14) throw glare::Exception("Profile contains consecutive duplicate points.");
				for(int i=0;i<segments;++i)
				{
					const double u=6.283185307179586*i/segments,v=6.283185307179586*(i+1)/segments;
					const Vec3d a(points[j].x*std::cos(u),points[j].x*std::sin(u),points[j].y),b(points[j].x*std::cos(v),points[j].x*std::sin(v),points[j].y);
					const Vec3d c(points[j+1].x*std::cos(v),points[j+1].x*std::sin(v),points[j+1].y),d(points[j+1].x*std::cos(u),points[j+1].x*std::sin(u),points[j+1].y);
					const Vec3d nu(dz*std::cos(u),dz*std::sin(u),-dr),nv(dz*std::cos(v),dz*std::sin(v),-dr);
					triangle(a,b,c,nu,nv,nv); triangle(a,c,d,nu,nv,nu);
				}
			}
			return; // Include radius=0 endpoints in the profile for caps, or a closed loop for hollow vessels.
		}
		const double depth=number(p,part,"depth",1,.001,1000);extrudeContour(p,list,depth);
	}
	void extrudeContour(const JSONParser& p,const JSONNode& list,double depth)
	{
		if(list.child_indices.size()<3 || list.child_indices.size()>128) throw glare::Exception("Extrusion contour needs 3..128 points.");
		std::vector<Vec3d> points;
		for(uint32 index:list.child_indices) { double v[2];p.nodes[index].parseDoubleArrayValues(p,2,v);if(!std::isfinite(v[0])||!std::isfinite(v[1])||std::abs(v[0])>1000||std::abs(v[1])>1000) throw glare::Exception("Invalid extrusion contour coordinate.");points.push_back(Vec3d(v[0],v[1],0)); }
		auto cross=[](const Vec3d& a,const Vec3d& b,const Vec3d& c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); };
		double area=0;
		for(size_t i=0;i<points.size();++i) { const Vec3d& a=points[i];const Vec3d& b=points[(i+1)%points.size()]; area+=a.x*b.y-b.x*a.y; }
		if(std::abs(area)<1e-10) throw glare::Exception("Extrusion contour has zero area.");
		if(area<0) std::reverse(points.begin(),points.end());
		// Reject intersections, repeated points and collinear adjacent edges before ear clipping.
		for(size_t i=0;i<points.size();++i)
		{
			const size_t next=(i+1)%points.size();
			if(std::abs(cross(points[i],points[next],points[(i+2)%points.size()]))<1e-10) throw glare::Exception("Remove duplicate/collinear contour vertices.");
			for(size_t j=i+1;j<points.size();++j)
			{
				const size_t jnext=(j+1)%points.size();
				if(j==next || jnext==i) continue;
				const double a=cross(points[i],points[next],points[j]),b=cross(points[i],points[next],points[jnext]);
				const double c=cross(points[j],points[jnext],points[i]),d=cross(points[j],points[jnext],points[next]);
				if(a*b<=0 && c*d<=0 &&
					myMax(myMin(points[i].x,points[next].x),myMin(points[j].x,points[jnext].x))<=myMin(myMax(points[i].x,points[next].x),myMax(points[j].x,points[jnext].x)) &&
					myMax(myMin(points[i].y,points[next].y),myMin(points[j].y,points[jnext].y))<=myMin(myMax(points[i].y,points[next].y),myMax(points[j].y,points[jnext].y))) throw glare::Exception("Extrusion contour must be simple without touching/intersecting edges.");
			}
		}
		std::vector<int> polygon;
		for(size_t i=0;i<points.size();++i) polygon.push_back((int)i);
		while(polygon.size()>2)
		{
			bool found=false;
			for(size_t i=0;i<polygon.size();++i)
			{
				const int ai=polygon[(i+polygon.size()-1)%polygon.size()],bi=polygon[i],ci=polygon[(i+1)%polygon.size()];
				const Vec3d a=points[ai],b=points[bi],c=points[ci];
				if(cross(a,b,c)<=1e-10) continue;
				bool occupied=false;
				for(int k:polygon) if(k!=ai && k!=bi && k!=ci && cross(a,b,points[k])>=-1e-10 && cross(b,c,points[k])>=-1e-10 && cross(c,a,points[k])>=-1e-10) { occupied=true;break; }
				if(occupied) continue;
				triangle(a+Vec3d(0,0,depth/2),b+Vec3d(0,0,depth/2),c+Vec3d(0,0,depth/2));
				triangle(c-Vec3d(0,0,depth/2),b-Vec3d(0,0,depth/2),a-Vec3d(0,0,depth/2));
				polygon.erase(polygon.begin()+i); found=true;break;
			}
			if(!found) throw glare::Exception("Cannot triangulate extrusion contour.");
		}
		for(size_t i=0;i<points.size();++i)
		{
			const Vec3d a=points[i],b=points[(i+1)%points.size()],z(0,0,depth/2);
			quad(a-z,b-z,b+z,a+z);
		}
	}
	void extrudeWithHoles(const JSONParser& p,const JSONNode& part)
	{
		const JSONNode& outer=part.getChildArray(p,"profile");const double depth=number(p,part,"depth",1,.001,1000);
		if(!part.hasChild("holes")) { extrudeContour(p,outer,depth);return; }
		const JSONNode& holes=part.getChildArray(p,"holes");if(holes.child_indices.empty()||holes.child_indices.size()>16) throw glare::Exception("Extrusion supports 1..16 inner contours.");
		auto read2D=[&](const JSONNode& contour) { if(contour.child_indices.size()<3||contour.child_indices.size()>128) throw glare::Exception("Each extrusion contour needs 3..128 points.");std::vector<std::array<double,2>> result;
			for(uint32 index:contour.child_indices) { double xy[2];p.nodes[index].parseDoubleArrayValues(p,2,xy);if(!std::isfinite(xy[0])||!std::isfinite(xy[1])||std::abs(xy[0])>1000||std::abs(xy[1])>1000) throw glare::Exception("Invalid hole contour coordinate.");result.push_back({xy[0],xy[1]}); }return result; };
		auto cross2=[](const auto& a,const auto& b,const auto& c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);};
		auto intersects=[&](const auto& a,const auto& b,const auto& c,const auto& d) {
			if(std::max(std::min(a[0],b[0]),std::min(c[0],d[0]))>std::min(std::max(a[0],b[0]),std::max(c[0],d[0]))+1e-10||
				std::max(std::min(a[1],b[1]),std::min(c[1],d[1]))>std::min(std::max(a[1],b[1]),std::max(c[1],d[1]))+1e-10) return false;
			const double x=cross2(a,b,c),y=cross2(a,b,d),z=cross2(c,d,a),q=cross2(c,d,b),eps=1e-10;
			return ((x<=eps&&y>=-eps)||(y<=eps&&x>=-eps))&&((z<=eps&&q>=-eps)||(q<=eps&&z>=-eps));
		};
		auto inside=[](const std::array<double,2>& point,const std::vector<std::array<double,2>>& polygon) { bool result=false;for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++) { const auto& a=polygon[i];const auto& b=polygon[j];if(((a[1]>point[1])!=(b[1]>point[1]))&&(point[0]<(b[0]-a[0])*(point[1]-a[1])/(b[1]-a[1])+a[0])) result=!result; }return result; };
		const auto outer_points=read2D(outer);std::vector<std::vector<std::array<double,2>>> hole_points;
		for(uint32 index:holes.child_indices) { auto loop=read2D(p.nodes[index]);for(size_t i=0;i<loop.size();++i) {
			if(!inside(loop[i],outer_points)) throw glare::Exception("Every hole contour must lie strictly inside the outer extrusion contour.");
			for(size_t j=0;j<outer_points.size();++j) if(intersects(loop[i],loop[(i+1)%loop.size()],outer_points[j],outer_points[(j+1)%outer_points.size()])) throw glare::Exception("Hole contours may not touch or cross the outer contour.");
		}hole_points.push_back(std::move(loop)); }
		for(size_t a=0;a<hole_points.size();++a) for(size_t b=a+1;b<hole_points.size();++b) {
			for(size_t i=0;i<hole_points[a].size();++i) for(size_t j=0;j<hole_points[b].size();++j) if(intersects(hole_points[a][i],hole_points[a][(i+1)%hole_points[a].size()],hole_points[b][j],hole_points[b][(j+1)%hole_points[b].size()])) throw glare::Exception("Hole contours may not intersect or touch.");
			if(inside(hole_points[a][0],hole_points[b])||inside(hole_points[b][0],hole_points[a])) throw glare::Exception("Nested hole contours are not supported.");
		}
		MeshBuilder base;base.material=material;base.extrudeContour(p,outer,depth);MCPMeshOps::Mesh current=operationsMesh(base.mesh);
		for(uint32 index:holes.child_indices) {
			const JSONNode& contour=p.nodes[index];MeshBuilder cutter;cutter.material=material;cutter.extrudeContour(p,contour,depth+std::max(.0001,depth*.0001));
			try { current=MCPMeshOps::booleanMesh(current,operationsMesh(cutter.mesh),"subtract"); }
			catch(const std::exception& e) { throw glare::Exception(std::string("Extrusion hole failed: ")+e.what()); }
			if(current.empty()) throw glare::Exception("Extrusion hole removed the entire profile.");
		}
		appendOperationsMesh(current);
	}
	void roundedBox(int steps, double radius)
	{
		// Six subdivided cube faces projected onto a rounded box, with analytic smooth normals.
		for(int axis=0;axis<3;++axis) for(int sign=-1;sign<=1;sign+=2)
		{
			auto at=[&](int i,int j,Vec3d& normal) {
				double v[3]={0,0,0},c[3]; v[axis]=sign*.5;v[(axis+1)%3]=-.5+(double)i/steps;v[(axis+2)%3]=-.5+(double)j/steps;
				for(int k=0;k<3;++k) c[k]=myClamp(v[k],-.5+radius,.5-radius);
				normal=Vec3d(v[0]-c[0],v[1]-c[1],v[2]-c[2]); normal=normal/normal.length();
				return Vec3d(c[0],c[1],c[2])+normal*radius;
			};
			for(int i=0;i<steps;++i) for(int j=0;j<steps;++j)
			{
				Vec3d na,nb,nc,nd;const Vec3d a=at(i,j,na),b=at(i+1,j,nb),c=at(i+1,j+1,nc),d=at(i,j+1,nd);
				if(sign>0) { triangle(a,b,c,na,nb,nc);triangle(a,c,d,na,nc,nd); }
				else { triangle(c,b,a,nc,nb,na);triangle(d,c,a,nd,nc,na); }
			}
		}
	}
	void primitive(const std::string& shape,int segments,int rings,double tube)
	{
		const double pi=3.141592653589793;
		if(appendLibraryPrimitive(*this,shape)) return;
		if(shape=="box")
		{
			const Vec3d a(-.5,-.5,-.5),b(.5,-.5,-.5),c(.5,.5,-.5),d(-.5,.5,-.5),e(-.5,-.5,.5),f(.5,-.5,.5),g(.5,.5,.5),h(-.5,.5,.5);
			quad(a,d,c,b);quad(e,f,g,h);quad(a,b,f,e);quad(b,c,g,f);quad(c,d,h,g);quad(d,a,e,h);
		}
		else if(shape=="sphere" || shape=="torus")
		{
			auto at=[&](int i,int j) {
				const double u=2*pi*i/segments,v=(shape=="sphere" ? -pi/2+pi*j/rings : 2*pi*j/rings);
				const double r=shape=="sphere" ? .5*std::cos(v) : .5-tube+tube*std::cos(v);
				return Vec3d(r*std::cos(u),r*std::sin(u),shape=="sphere" ? .5*std::sin(v) : tube*std::sin(v));
			};
			auto normal=[&](int i,int j) {
				if(shape=="sphere") return at(i,j);
				const double u=2*pi*i/segments,v=2*pi*j/rings;
				return Vec3d(std::cos(v)*std::cos(u),std::cos(v)*std::sin(u),std::sin(v));
			};
			for(int i=0;i<segments;++i) for(int j=0;j<rings;++j)
			{
				triangle(at(i,j),at(i+1,j),at(i+1,j+1),normal(i,j),normal(i+1,j),normal(i+1,j+1));
				triangle(at(i,j),at(i+1,j+1),at(i,j+1),normal(i,j),normal(i+1,j+1),normal(i,j+1));
			}
		}
		else if(shape=="cylinder" || shape=="cone")
		{
			for(int i=0;i<segments;++i)
			{
				const double u=2*pi*i/segments,v=2*pi*(i+1)/segments,r=shape=="cone"?0:.5;
				Vec3d a(.5*std::cos(u),.5*std::sin(u),-.5),b(.5*std::cos(v),.5*std::sin(v),-.5),c(r*std::cos(v),r*std::sin(v),.5),d(r*std::cos(u),r*std::sin(u),.5);
				const Vec3d nu(std::cos(u),std::sin(u),shape=="cone"?.5:0),nv(std::cos(v),std::sin(v),shape=="cone"?.5:0);
				triangle(a,b,c,nu,nv,nv);triangle(a,c,d,nu,nv,nu);
				triangle(Vec3d(0,0,-.5),b,a);triangle(Vec3d(0,0,.5),d,c);
			}
		}
		else throw glare::Exception("Unknown primitive shape: "+shape);
	}
	void sweptTube(const JSONParser& p,const JSONNode& part,int segments,double radius,bool closed)
	{
		const JSONNode& array=part.getChildArray(p,"path");std::vector<Vec3d> path;path.reserve(array.child_indices.size());
		for(uint32 index:array.child_indices) { double v[3];p.nodes[index].parseDoubleArrayValues(p,3,v);for(double x:v) if(!std::isfinite(x)||std::abs(x)>500) throw glare::Exception("Tube path coordinates must be finite and within +/-500 metres.");path.push_back(Vec3d(v[0],v[1],v[2])); }
		if(closed&&path.size()>2&&(path.front()-path.back()).length()<1e-8) path.pop_back();
		if(path.size()<(closed?3u:2u)||path.size()>128) throw glare::Exception("Tube path requires 2..128 points, or 3..128 when closed.");
		for(size_t i=1;i<path.size();++i) if((path[i]-path[i-1]).length()<1e-8) throw glare::Exception("Tube path has duplicate consecutive points.");
		if(closed&&(path.front()-path.back()).length()<1e-8) throw glare::Exception("Closed tube path repeats its first point; omit the duplicate endpoint.");
		const size_t count=path.size();
		auto dot3=[](const Vec3d& a,const Vec3d& b){return a.x*b.x+a.y*b.y+a.z*b.z;};
		auto cross3=[](const Vec3d& a,const Vec3d& b){return Vec3d(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);};
		auto unit3=[&](const Vec3d& v){const double n=std::sqrt(dot3(v,v));return n>1e-12?v/n:Vec3d(0,0,0);};
		auto rotate3=[&](const Vec3d& v,const Vec3d& axis,double angle){const double c=std::cos(angle),s=std::sin(angle);return v*c+cross3(axis,v)*s+axis*(dot3(axis,v)*(1-c));};
		std::vector<Vec3d> tangents(count),frame_n(count),frame_b(count);
		for(size_t i=0;i<count;++i) {
			const Vec3d before=path[(i+count-1)%count],after=path[(i+1)%count];
			const Vec3d d=(closed||(i>0&&i+1<count))?after-before:(i==0?path[1]-path[0]:path[count-1]-path[count-2]);
			tangents[i]=unit3(d);if(tangents[i].length()<.5) throw glare::Exception("Tube path has a reversing corner; add an intermediate point.");
		}
		const Vec3d reference=std::abs(tangents[0].z)<.8?Vec3d(0,0,1):Vec3d(0,1,0);
		frame_n[0]=unit3(cross3(tangents[0],reference));frame_b[0]=unit3(cross3(tangents[0],frame_n[0]));
		for(size_t i=1;i<count;++i) {
			const Vec3d raw=cross3(tangents[i-1],tangents[i]);const double s=raw.length(),c=dot3(tangents[i-1],tangents[i]);
			if(c<-.9999) throw glare::Exception("Tube path reverses direction too sharply; add intermediate points.");
			Vec3d normal=frame_n[i-1];if(s>1e-10) normal=rotate3(normal,raw/s,std::atan2(s,c));
			frame_n[i]=unit3(normal-tangents[i]*dot3(normal,tangents[i]));frame_b[i]=unit3(cross3(tangents[i],frame_n[i]));
		}
		if(closed) {
			const Vec3d raw=cross3(tangents.back(),tangents.front());const double s=raw.length(),c=dot3(tangents.back(),tangents.front());
			if(c<-.9999) throw glare::Exception("Closed tube path has an unsupported 180-degree corner.");
			Vec3d end_n=frame_n.back();if(s>1e-10) end_n=rotate3(end_n,raw/s,std::atan2(s,c));
			const double twist=std::atan2(dot3(tangents.front(),cross3(end_n,frame_n.front())),dot3(end_n,frame_n.front()));
			for(size_t i=1;i<count;++i) { frame_n[i]=unit3(rotate3(frame_n[i],tangents[i],twist*(double)i/count));frame_b[i]=unit3(cross3(tangents[i],frame_n[i])); }
		}
		std::vector<double> distance(count,0);for(size_t i=1;i<count;++i) distance[i]=distance[i-1]+(path[i]-path[i-1]).length();
		const double total=distance.back()+(closed?(path.front()-path.back()).length():0);
		std::vector<std::vector<Vec3d>> rings(count,std::vector<Vec3d>((size_t)segments)),normals(count,std::vector<Vec3d>((size_t)segments));
		for(size_t i=0;i<count;++i) for(int j=0;j<segments;++j) { const double a=6.283185307179586*(double)j/segments;normals[i][j]=unit3(frame_n[i]*std::cos(a)+frame_b[i]*std::sin(a));rings[i][j]=path[i]+normals[i][j]*radius; }
		const size_t edge_count=closed?count:count-1;
		for(size_t i=0;i<edge_count;++i) { const size_t next=(i+1)%count;const double next_d=next==0?total:distance[next];
			for(int j=0;j<segments;++j) { const int k=(j+1)%segments;const double u0=(double)j/segments,u1=(double)(j+1)/segments;
				const Vec3d a=rings[i][j],b=rings[next][j],c=rings[next][k],d=rings[i][k];
				triangle(a,c,b,normals[i][j],normals[next][k],normals[next][j]);setLastTriangleUV(u0,distance[i],u1,next_d,u0,next_d);
				triangle(a,d,c,normals[i][j],normals[i][k],normals[next][k]);setLastTriangleUV(u0,distance[i],u1,distance[i],u1,next_d);
			}
		}
		if(!closed) for(int j=0;j<segments;++j) { const int k=(j+1)%segments;
			triangle(path.front(),rings[0][k],rings[0][j],tangents.front()*-1,normals[0][k],normals[0][j]);
			triangle(path.back(),rings.back()[j],rings.back()[k],tangents.back(),normals.back()[j],normals.back()[k]);
		}
	}
	void sweptRoad(const JSONParser& p,const JSONNode& part,int samples_per_span,double width,double thickness,bool smooth)
	{
		const JSONNode& array=part.getChildArray(p,"path");std::vector<Vec3d> anchors;anchors.reserve(array.child_indices.size());
		for(uint32 index:array.child_indices) { double v[3];p.nodes[index].parseDoubleArrayValues(p,3,v);for(double x:v) if(!std::isfinite(x)||std::abs(x)>500) throw glare::Exception("Road path coordinates must be finite and within +/-500 metres.");anchors.push_back(Vec3d(v[0],v[1],v[2])); }
		if(anchors.size()<2||anchors.size()>128) throw glare::Exception("Road path requires 2..128 local-space points.");
		for(size_t i=1;i<anchors.size();++i) if((anchors[i]-anchors[i-1]).length()<1e-6) throw glare::Exception("Road path has duplicate consecutive points.");
		std::vector<Vec3d> path;path.reserve((anchors.size()-1)*(size_t)samples_per_span+1);
		for(size_t span=0;span+1<anchors.size();++span)
		{
			const Vec3d p1=anchors[span],p2=anchors[span+1];
			const Vec3d p0=span==0?p1*2-p2:anchors[span-1];
			const Vec3d p3=span+2<anchors.size()?anchors[span+2]:p2*2-p1;
			for(int step=0;step<samples_per_span;++step)
			{
				const double t=(double)step/samples_per_span;
				if(smooth)
				{
					const double t2=t*t,t3=t2*t;
					path.push_back((p1*2+(p2-p0)*t+(p0*2-p1*5+p2*4-p3)*t2+(p0*-1+p1*3-p2*3+p3)*t3)*.5);
				}
				else path.push_back(p1+(p2-p1)*t);
			}
		}
		path.push_back(anchors.back());
		for(const Vec3d& point:path) if(std::abs(point.x)>500||std::abs(point.y)>500||std::abs(point.z)>500) throw glare::Exception("Smoothed road path exceeds +/-500 metres; reduce or split the local path.");
		const size_t count=path.size();std::vector<Vec3d> side(count),left(count),right(count),left_bottom(count),right_bottom(count);std::vector<double> distance(count,0);
		for(size_t i=0;i<count;++i)
		{
			const Vec3d tangent=i==0?path[1]-path[0]:(i+1==count?path[i]-path[i-1]:path[i+1]-path[i-1]);
			const double horizontal=std::sqrt(tangent.x*tangent.x+tangent.y*tangent.y);
			if(horizontal<1e-8) throw glare::Exception("Road path must make horizontal progress at every point.");
			side[i]=Vec3d(-tangent.y/horizontal,tangent.x/horizontal,0);
			left[i]=path[i]+side[i]*(width*.5);right[i]=path[i]-side[i]*(width*.5);
			left_bottom[i]=left[i]+Vec3d(0,0,-thickness);right_bottom[i]=right[i]+Vec3d(0,0,-thickness);
			if(i>0) distance[i]=distance[i-1]+(path[i]-path[i-1]).length();
		}
		auto uvTriangle=[&](const Vec3d& a,const Vec3d& b,const Vec3d& c,double au,double av,double bu,double bv,double cu,double cv)
		{
			triangle(a,b,c);setLastTriangleUV(au,av,bu,bv,cu,cv);
		};
		for(size_t i=0;i+1<count;++i)
		{
			const double u0=0,u1=width,v0=distance[i],v1=distance[i+1];
			uvTriangle(left[i],right[i],right[i+1],u0,v0,u1,v0,u1,v1);
			uvTriangle(left[i],right[i+1],left[i+1],u0,v0,u1,v1,u0,v1);
			triangle(right_bottom[i],left_bottom[i],left_bottom[i+1]);triangle(right_bottom[i],left_bottom[i+1],right_bottom[i+1]);
			triangle(left[i],left[i+1],left_bottom[i+1]);triangle(left[i],left_bottom[i+1],left_bottom[i]);
			triangle(right[i],right_bottom[i],right_bottom[i+1]);triangle(right[i],right_bottom[i+1],right[i+1]);
		}
		triangle(left.front(),left_bottom.front(),right_bottom.front());triangle(left.front(),right_bottom.front(),right.front());
		triangle(right.back(),right_bottom.back(),left_bottom.back());triangle(right.back(),left_bottom.back(),left.back());
	}
	void appendPart(const JSONParser& p,const JSONNode& part,int material_count)
	{
		center=vector(p,part,"center",Vec3d(0,0,0),-500,500);size=vector(p,part,"size",Vec3d(1,1,1),.001,1000);
		rotation=vector(p,part,"rotation",Vec3d(0,0,0),-3600,3600);material=integer(p,part,"material",0,0,material_count-1);
		const int repeat=integer(p,part,"repeat",1,1,128);const Vec3d start=center,angle=rotation;
		const Vec3d step=vector(p,part,"step",Vec3d(0,0,0),-500,500),angle_step=vector(p,part,"rotation_step",Vec3d(0,0,0),-360,360);
		for(int copy=0;copy<repeat;++copy) {
			center=start+step*(double)copy;rotation=angle+angle_step*(double)copy;
			const std::string shape=part.getChildStringValue(p,"shape");const int segments=integer(p,part,"segments",32,3,128);
			if(shape=="box"||shape=="deformed_box") boxPart(p,part,shape=="deformed_box",material_count);
			else if(part.hasChild("face_materials")||part.hasChild("vertices")) throw glare::Exception("Per-part vertices/face_materials are supported only for box/deformed_box.");
			else if(shape=="lathe") profile(p,part,true,segments);
			else if(shape=="extrude") extrudeWithHoles(p,part);
			else if(shape=="rounded_box") roundedBox(integer(p,part,"rings",12,3,32),number(p,part,"radius",.1,.001,.5));
			else if(shape=="beveled_box") {
				const Vec3d part_size=size;const int part_material=material;const double bevel=number(p,part,"bevel",.05,.0001,100);
				const unsigned int bevel_material=(unsigned int)integer(p,part,"bevel_material",part_material,0,material_count-1);
				try { const auto beveled=MCPMeshOps::bevelBox(MCPMeshOps::Vec3(part_size.x,part_size.y,part_size.z),bevel,(unsigned int)part_material,bevel_material);
					size=Vec3d(1,1,1);appendOperationsMesh(beveled);size=part_size;material=part_material;
				} catch(const std::exception& e) { throw glare::Exception(e.what()); }
			}
			else if(shape=="tube") sweptTube(p,part,integer(p,part,"segments",12,3,64),number(p,part,"tube_radius",.05,.001,100),part.getChildBoolValueWithDefaultVal(p,"closed",false));
			else if(shape=="road") sweptRoad(p,part,integer(p,part,"samples_per_span",10,2,32),number(p,part,"width",6,.1,100),number(p,part,"thickness",.5,.02,50),part.getChildBoolValueWithDefaultVal(p,"smooth",true));
			else primitive(shape,segments,integer(p,part,"rings",16,3,64),number(p,part,"tube",.15,.01,.24));
		}
	}
};

static std::string meshObject(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	if(n.hasChild("parts") && (n.hasChild("vertices") || n.hasChild("triangles"))) throw glare::Exception("Provide parts OR vertices and triangles, not both.");
	WorldObjectRef ob=new WorldObject(); ob->pos=Vec3d(0,0,0); ob->object_type=WorldObject::ObjectType_Generic; ob->axis=Vec3f(0,0,1); ob->angle=0; ob->scale=Vec3f(1,1,1);
	materials(p,n,*ob);
	MeshBuilder b;
	if(n.hasChild("parts"))
	{
		const JSONNode& parts=n.getChildArray(p,"parts");
		if(parts.child_indices.empty() || parts.child_indices.size()>512) throw glare::Exception("Use 1..512 primitive parts per mesh.");
		int total_parts=0;
		for(uint32 index:parts.child_indices)
		{
			const JSONNode& part=p.nodes[index];
			const int repeat=integer(p,part,"repeat",1,1,128);
			total_parts+=repeat;
			if(total_parts>512) throw glare::Exception("Expanded repetition exceeds 512 parts.");
			const std::string operation=part.getChildStringValueWithDefaultVal(p,"operation","add");
			if(operation=="add") b.appendPart(p,part,(int)ob->materials.size());
			else {
				if(operation!="union"&&operation!="subtract"&&operation!="intersect") throw glare::Exception("Part operation must be add, union, subtract or intersect.");
				if(b.mesh.triangles.empty()) throw glare::Exception("A boolean part needs earlier base geometry in the parts list.");
				MeshBuilder cutter;cutter.appendPart(p,part,(int)ob->materials.size());
				try {
					const MCPMeshOps::Mesh base=operationsMesh(b.mesh),tool=operationsMesh(cutter.mesh);
					const MCPMeshOps::Mesh result=MCPMeshOps::booleanMesh(base,tool,operation);
					if(result.empty()) throw glare::Exception("Boolean part produced an empty mesh.");
					b.mesh.vert_positions.resize(0);b.mesh.vert_normals.resize(0);b.mesh.vert_colours.resize(0);
					b.mesh.uv_pairs.resize(0);b.mesh.triangles.resize(0);b.mesh.quads.resize(0);b.mesh.used_materials.resize(0);
					b.mesh.num_uv_mappings=0;b.appendOperationsMesh(result);
				} catch(const glare::Exception&) { throw; }
				catch(const std::exception& e) { throw glare::Exception(std::string("Inline boolean part failed: ")+e.what()); }
			}
		}
	}
	else
	{
		const JSONNode& verts=n.getChildArray(p,"vertices"),& faces=n.getChildArray(p,"triangles");
		if(verts.child_indices.size()<3 || verts.child_indices.size()>50000 || faces.child_indices.empty() || faces.child_indices.size()>100000) throw glare::Exception("Mesh requires 3..50000 vertices and 1..100000 triangles.");
		std::vector<Vec3d> vertices;
		for(uint32 index:verts.child_indices)
		{
			double v[3];p.nodes[index].parseDoubleArrayValues(p,3,v);
			for(int i=0;i<3;++i) if(!std::isfinite(v[i]) || std::abs(v[i])>1000) throw glare::Exception("Mesh vertices must be finite and within +/-1000 metres.");
			vertices.push_back(Vec3d(v[0],v[1],v[2]));
		}
		for(uint32 index:faces.child_indices)
		{
			const JSONNode& f=p.nodes[index];double vi[3];f.getChildArray(p,"indices").parseDoubleArrayValues(p,3,vi);
			for(int j=0;j<3;++j) if(!std::isfinite(vi[j]) || vi[j]!=std::floor(vi[j]) || vi[j]<0 || vi[j]>=vertices.size()) throw glare::Exception("Triangle index is invalid.");
			b.material=integer(p,f,"material",0,0,(int)ob->materials.size()-1);
			const size_t before=b.mesh.triangles.size();
			b.triangle(vertices[(size_t)vi[0]],vertices[(size_t)vi[1]],vertices[(size_t)vi[2]]);
			if(before==b.mesh.triangles.size()) throw glare::Exception("Mesh contains a degenerate triangle.");
			if(f.hasChild("uv")) { double uv[6]; f.getChildArray(p,"uv").parseDoubleArrayValues(p,6,uv); b.setLastTriangleUV(uv[0],uv[1],uv[2],uv[3],uv[4],uv[5]); }
		}
	}
	b.mesh.endOfModel();
	Reference<BatchedMesh> mesh=BatchedMesh::buildFromIndigoMesh(b.mesh);
	mesh->checkValidAndSanitiseMesh(); mesh->optimise(); ob->setAABBOS(mesh->aabb_os);
	return publish(all,w,ob,p,n,user,username,mesh);
}

static std::string voxelObject(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	WorldObjectRef ob=new WorldObject();ob->pos=Vec3d(0,0,0);ob->object_type=WorldObject::ObjectType_VoxelGroup;ob->axis=Vec3f(0,0,1);ob->angle=0;
	ob->scale=Vec3f((float)number(p,n,"scale_x",.1,.001,100),(float)number(p,n,"scale_y",.1,.001,100),(float)number(p,n,"scale_z",.1,.001,100));
	materials(p,n,*ob);
	std::map<std::array<int,3>,int> cells;
	if(n.hasChild("voxels"))
	{
		const JSONNode& a=n.getChildArray(p,"voxels");
		if(a.child_indices.size()>262144) throw glare::Exception("Voxel budget exceeded.");
		for(uint32 index:a.child_indices)
		{
			const JSONNode& v=p.nodes[index];
			if(!v.hasChild("x") || !v.hasChild("y") || !v.hasChild("z")) throw glare::Exception("Each voxel requires x, y and z.");
			cells[{{integer(p,v,"x",0,-512,511),integer(p,v,"y",0,-512,511),integer(p,v,"z",0,-512,511)}}]=integer(p,v,"material",0,0,(int)ob->materials.size()-1);
		}
	}
	uint64 work=0;
	if(n.hasChild("operations"))
	{
		const JSONNode& ops=n.getChildArray(p,"operations");
		if(ops.child_indices.size()>512) throw glare::Exception("Use at most 512 voxel operations.");
		for(uint32 index:ops.child_indices)
		{
			const JSONNode& v=p.nodes[index];
			if(!v.hasChild("min") || !v.hasChild("size")) throw glare::Exception("Each voxel operation requires min and size.");
			const Vec3d origin=vector(p,v,"min",Vec3d(0,0,0),-512,511),extent=vector(p,v,"size",Vec3d(1,1,1),1,256);
			const double ov[]={origin.x,origin.y,origin.z},ev[]={extent.x,extent.y,extent.z};
			for(int j=0;j<3;++j) if(ov[j]!=std::floor(ov[j]) || ev[j]!=std::floor(ev[j]) || ov[j]+ev[j]>512) throw glare::Exception("Voxel bounds must be integer cell coordinates in [-512,512).");
			work+=(uint64)extent.x*(uint64)extent.y*(uint64)extent.z;
			if(work>4000000) throw glare::Exception("Voxel operation budget exceeded (4 million candidate cells).");
			const std::string shape=v.getChildStringValueWithDefaultVal(p,"shape","box"),mode=v.getChildStringValueWithDefaultVal(p,"mode","add");
			if(shape!="box" && shape!="ellipsoid" && shape!="cylinder" && shape!="cone" && shape!="torus" && shape!="wedge") throw glare::Exception("Unknown voxel shape.");
			if(mode!="add" && mode!="subtract" && mode!="paint") throw glare::Exception("Unknown voxel operation mode.");
			const std::string axis=v.getChildStringValueWithDefaultVal(p,"axis","z");
			if(axis!="x" && axis!="y" && axis!="z") throw glare::Exception("Voxel axis must be x, y or z.");
			const int shell=integer(p,v,"shell",0,0,128);
			const double tube=number(p,v,"tube",.35,.05,.95);
			auto inside=[&](double dx,double dy,double dz) {
				if(std::abs(dx)>1 || std::abs(dy)>1 || std::abs(dz)>1) return false;
				if(axis=="x") std::swap(dx,dz); else if(axis=="y") std::swap(dy,dz);
				if(shape=="ellipsoid") return dx*dx+dy*dy+dz*dz<=1;
				if(shape=="cylinder") return dx*dx+dy*dy<=1;
				if(shape=="cone") return dx*dx+dy*dy<=(1-dz)*(1-dz)*.25;
				if(shape=="torus") { const double r=std::sqrt(dx*dx+dy*dy)-(1-tube);return r*r+dz*dz*tube*tube<=tube*tube; }
				if(shape=="wedge") return dz<=dx;
				return true;
			};
			const int mat=integer(p,v,"material",0,0,(int)ob->materials.size()-1);
			for(int z=0;z<(int)extent.z;++z) for(int y=0;y<(int)extent.y;++y) for(int x=0;x<(int)extent.x;++x)
			{
				const double dx=(x+.5)/extent.x*2-1,dy=(y+.5)/extent.y*2-1,dz=(z+.5)/extent.z*2-1;
				if(!inside(dx,dy,dz)) continue;
				// A shell is the outer volume minus a concentric volume inset on each bounding-box axis.
				if(shell>0 && extent.x>2*shell && extent.y>2*shell && extent.z>2*shell &&
					inside(dx*extent.x/(extent.x-2*shell),dy*extent.y/(extent.y-2*shell),dz*extent.z/(extent.z-2*shell))) continue;
				const std::array<int,3> key={{(int)origin.x+x,(int)origin.y+y,(int)origin.z+z}};
				if(mode=="subtract") cells.erase(key);
				else if(mode=="add" || cells.count(key)) cells[key]=mat;
				if(cells.size()>262144) throw glare::Exception("Voxel budget exceeded (262144 occupied cells).");
			}
		}
	}
	if(cells.empty()) throw glare::Exception("Voxel operations produced an empty object.");
	for(const auto& c:cells) ob->getDecompressedVoxels().push_back(Voxel(Vec3<int>(c.first[0],c.first[1],c.first[2]),c.second));
	const js::AABBox box=ob->getDecompressedVoxelGroup().getAABB();
	for(int j=0;j<3;++j) if(box.max_[j]-box.min_[j]>256) throw glare::Exception("Voxel object extent exceeds 256 cells on an axis. Split the model into sections.");
	ob->setAABBOS(box);ob->compressVoxels();
	return publish(all,w,ob,p,n,user,username,Reference<BatchedMesh>());
}

static UID objectUID(const JSONParser& p,const JSONNode& n)
{
	if(!n.hasChild("uid")) throw glare::Exception("Object UID is required.");
	const double v=number(p,n,"uid",0,0,9007199254740991.0);
	if(v!=std::floor(v)) throw glare::Exception("UID must be an integer.");
	return UID((uint64)v);
}

static WorldObjectRef findObject(ServerWorldState& w,WorldStateLock& lock,UID uid)
{
	const auto it=w.getObjects(lock).find(uid);
	if(it==w.getObjects(lock).end() || it->second->state==WorldObject::State_Dead) throw glare::Exception("Object not found in the active world.");
	return it->second;
}

static MCPMeshOps::Mesh operationsMesh(const Indigo::Mesh& native, size_t max_triangles)
{
	if(native.triangles.empty() || native.triangles.size()>max_triangles) throw glare::Exception("Mesh exceeds the triangle limit for this operation.");
	MCPMeshOps::Mesh out;out.reserve(native.triangles.size());
	for(const auto& tri:native.triangles)
	{
		MCPMeshOps::Triangle t;t.material=tri.tri_mat_index;
		for(int k=0;k<3;++k)
		{
			const uint32 vi=tri.vertex_indices[k],ui=tri.uv_indices[k];
			if(vi>=native.vert_positions.size() || vi>=native.vert_normals.size() || ui>=native.uv_pairs.size()) throw glare::Exception("Mesh contains an invalid vertex or UV index.");
			const auto& v=native.vert_positions[vi];const auto& normal=native.vert_normals[vi];const auto& uv=native.uv_pairs[ui];
			t.vertices[k].position=MCPMeshOps::Vec3(v.x,v.y,v.z);t.vertices[k].normal=MCPMeshOps::Vec3(normal.x,normal.y,normal.z);t.vertices[k].u=uv.x;t.vertices[k].v=uv.y;
		}
		out.push_back(t);
	}
	return out;
}

static WorldObjectRef meshSnapshot(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& uid_key="uid")
{
	const double raw_uid=number(p,n,uid_key,0,0,9007199254740991.0);if(raw_uid!=std::floor(raw_uid)) throw glare::Exception("Object UID must be an integer.");
	const UID uid((uint64)raw_uid);WorldObjectRef snapshot=new WorldObject();
	{
		WorldStateLock lock(all.mutex);WorldObjectRef source=findObject(w,lock,uid);
		if(!editable(*source,w,user)) throw glare::Exception("Permission denied for this object.");
		if(source->object_type!=WorldObject::ObjectType_Generic || !hasExtension(toStdString(source->model_url),"bmesh")) throw glare::Exception("Mesh operations require an editable native .bmesh object.");
		snapshot->copyNetworkStateFrom(*source);
	}
	const std::string path=localMeshResourcePath(all,snapshot->model_url);
	if(FileUtils::getFileSize(path)>16*1024*1024) throw glare::Exception("Source mesh exceeds the 16 MiB MCP limit.");
	return snapshot;
}

// The server ResourceManager is in-memory only. After a restart, restore the resource
// entry when the URL's deterministic local file is still on disk.
static std::string localMeshResourcePath(ServerAllWorldsState& all,const URLString& url)
{
	if(!hasExtension(toStdString(url),"bmesh")) throw glare::Exception("Mesh geometry requires a native .bmesh resource.");
	const std::string path=all.resource_manager->pathForURL(url);
	if(!all.resource_manager->isFileForURLPresent(url) && FileUtils::fileExists(path))
		all.resource_manager->setResourceAsLocallyPresentForURL(url);
	if(!all.resource_manager->isFileForURLPresent(url))
		throw glare::Exception("The server has no local file for mesh resource '"+toStdString(url)+"'. A client-side cache is not accessible to MCP; restore or upload this model resource to the server.");
	return path;
}

static Reference<BatchedMesh> readNativeMesh(ServerAllWorldsState& all,const WorldObject& snapshot)
{
	Reference<BatchedMesh> mesh=BatchedMesh::readFromFile(all.resource_manager->pathForURL(snapshot.model_url),NULL);mesh->checkValidAndSanitiseMesh();return mesh;
}

static std::string deformMesh(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	WorldObjectRef ob=meshSnapshot(all,w,p,n,user,"replace_uid");Reference<BatchedMesh> native=readNativeMesh(all,*ob);Reference<Indigo::Mesh> source=native->buildIndigoMesh();
	MCPMeshOps::Deformation op;op.mode=n.getChildStringValue(p,"mode");
	const Vec3d center=vector(p,n,"center",Vec3d(0,0,0),-1000,1000),direction=vector(p,n,"direction",Vec3d(0,0,1),-1,1);
	op.center=MCPMeshOps::Vec3(center.x,center.y,center.z);op.direction=MCPMeshOps::Vec3(direction.x,direction.y,direction.z);
	op.radius=number(p,n,"radius",1,.001,10000);op.strength=number(p,n,"strength",.1,-1000,1000);op.axis=integer(p,n,"axis",2,0,2);
	op.min=number(p,n,"min",(double)native->aabb_os.min_[op.axis],-1000,1000);op.max=number(p,n,"max",(double)native->aabb_os.max_[op.axis],-1000,1000);
	op.amount=number(p,n,"amount",0,-6.283185307,6.283185307);op.iterations=integer(p,n,"iterations",1,1,20);
	MCPMeshOps::Mesh edited;try { edited=MCPMeshOps::deformMesh(operationsMesh(*source),op); } catch(const std::exception& e) { throw glare::Exception(e.what()); }
	MeshBuilder builder;builder.appendOperationsMesh(edited);builder.mesh.endOfModel();Reference<BatchedMesh> result=BatchedMesh::buildFromIndigoMesh(builder.mesh);result->checkValidAndSanitiseMesh();ob->setAABBOS(result->aabb_os);
	const std::vector<WorldObjectRef> sources{ob};return publish(all,w,ob,p,n,user,username,result,&sources);
}

static std::string editMeshTopology(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	WorldObjectRef ob=meshSnapshot(all,w,p,n,user,"replace_uid");Reference<BatchedMesh> native=readNativeMesh(all,*ob);Reference<Indigo::Mesh> source=native->buildIndigoMesh();
	const MCPMeshOps::Mesh input=operationsMesh(*source);const std::string mode=n.getChildStringValue(p,"mode");MCPMeshOps::Mesh edited;
	try {
		if(mode=="extrude_faces"||mode=="inset_faces"||mode=="set_face_material") {
			const JSONNode& face_list=n.getChildArray(p,"faces");if(face_list.child_indices.empty()||face_list.child_indices.size()>4096) throw glare::Exception("Select 1..4096 triangle faces from get_geometry output.");
			std::vector<double> raw(face_list.child_indices.size());face_list.parseDoubleArrayValues(p,raw.size(),raw.data());std::vector<size_t> faces;faces.reserve(raw.size());
			for(double f:raw) { if(!std::isfinite(f)||f<0||f>=input.size()||f!=std::floor(f)) throw glare::Exception("Face indices must be integer triangle indices from get_geometry.");faces.push_back((size_t)f); }
			if(mode=="set_face_material") {
				const int material=integer(p,n,"material",0,0,(int)ob->materials.size()-1);edited=input;
				for(size_t face:faces) edited[face].material=(unsigned int)material;
			} else if(mode=="extrude_faces") edited=MCPMeshOps::extrudeFaceRegion(input,faces,number(p,n,"distance",.1,-1000,1000));
			else edited=MCPMeshOps::insetFaceRegion(input,faces,number(p,n,"distance",.05,.0001,1000));
		} else if(mode=="bevel_edges") {
			const JSONNode& edge_list=n.getChildArray(p,"edges");if(edge_list.child_indices.empty()||edge_list.child_indices.size()>64) throw glare::Exception("Select 1..64 edges as endpoint pairs from get_geometry local coordinates.");
			std::vector<std::array<MCPMeshOps::Vec3,2>> edges;edges.reserve(edge_list.child_indices.size());
			for(uint32 edge_index:edge_list.child_indices) { const JSONNode& edge=p.nodes[edge_index];if(edge.child_indices.size()!=2) throw glare::Exception("Each bevel edge must contain exactly two local-space endpoints.");std::array<MCPMeshOps::Vec3,2> pair;
				for(int endpoint=0;endpoint<2;++endpoint) { double v[3];p.nodes[edge.child_indices[endpoint]].parseDoubleArrayValues(p,3,v);pair[endpoint]=MCPMeshOps::Vec3(v[0],v[1],v[2]); }
				edges.push_back(pair);
			}
			const int material=integer(p,n,"material",0,0,(int)ob->materials.size()-1);edited=MCPMeshOps::bevelEdges(input,edges,number(p,n,"width",.05,.0001,100),(unsigned int)material);
		} else throw glare::Exception("Topology mode must be extrude_faces, inset_faces, set_face_material or bevel_edges.");
	} catch(const glare::Exception&) { throw; }
	catch(const std::exception& e) { throw glare::Exception(e.what()); }
	MeshBuilder builder;builder.appendOperationsMesh(edited);builder.mesh.endOfModel();Reference<BatchedMesh> result=BatchedMesh::buildFromIndigoMesh(builder.mesh);result->checkValidAndSanitiseMesh();ob->setAABBOS(result->aabb_os);
	const std::vector<WorldObjectRef> sources{ob};return publish(all,w,ob,p,n,user,username,result,&sources);
}

static std::string unwrapMeshUV(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	WorldObjectRef ob=meshSnapshot(all,w,p,n,user,"replace_uid");Reference<BatchedMesh> source=readNativeMesh(all,*ob);
	Reference<Indigo::Mesh> native=source->buildIndigoMesh();
	if(native->triangles.empty()&&native->quads.empty()) throw glare::Exception("UV unwrap requires a mesh with faces.");
	if(native->triangles.size()+native->quads.size()>40000) throw glare::Exception("UV unwrap is limited to 40000 faces.");
	if(native->num_uv_mappings==0) throw glare::Exception("Source mesh has no UV mapping storage.");
	const size_t sets=native->num_uv_mappings, uv_count=native->uv_pairs.size()/sets;
	if(native->uv_pairs.size()%sets!=0) throw glare::Exception("Source mesh has malformed UV mapping storage.");
	if(native->uv_layout==Indigo::MESH_UV_LAYOUT_LAYER_VERTEX) {
		std::vector<Indigo::Vec2f> reordered(native->uv_pairs.size());
		for(size_t uv=0;uv<uv_count;++uv) for(size_t layer=0;layer<sets;++layer)
			reordered[uv*sets+layer]=native->uv_pairs[layer*uv_count+uv];
		native->uv_pairs.resize(reordered.size());for(size_t i=0;i<reordered.size();++i) native->uv_pairs[i]=reordered[i];
		native->uv_layout=Indigo::MESH_UV_LAYOUT_VERTEX_LAYER;
	} else if(native->uv_layout!=Indigo::MESH_UV_LAYOUT_VERTEX_LAYER) throw glare::Exception("Unsupported source UV layout.");
	const float margin=(float)number(p,n,"margin",.002,.0001,.05);StandardPrintOutput output;
	UVUnwrapper::Results unwrapped;
	try { unwrapped=UVUnwrapper::build(*native,Matrix4f::identity(),output,margin); }
	catch(const std::exception& e) { throw glare::Exception(std::string("UV unwrap failed: ")+e.what()); }
	if(unwrapped.num_patches==0||native->num_uv_mappings!=sets+1) throw glare::Exception("UV unwrap did not produce a usable atlas.");
	for(size_t uv=0;uv<native->uv_pairs.size()/native->num_uv_mappings;++uv)
		std::swap(native->uv_pairs[uv*native->num_uv_mappings],native->uv_pairs[uv*native->num_uv_mappings+sets]);
	Reference<BatchedMesh> result=BatchedMesh::buildFromIndigoMesh(*native);result->checkValidAndSanitiseMesh();ob->setAABBOS(result->aabb_os);
	const std::vector<WorldObjectRef> sources{ob};return publish(all,w,ob,p,n,user,username,result,&sources);
}

static std::string booleanMesh(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	const UID target_id((uint64)number(p,n,"replace_uid",0,0,9007199254740991.0)),operand_id((uint64)number(p,n,"operand_uid",0,0,9007199254740991.0));
	if(target_id==operand_id || (double)target_id.value()!=number(p,n,"replace_uid",0,0,9007199254740991.0) || (double)operand_id.value()!=number(p,n,"operand_uid",0,0,9007199254740991.0)) throw glare::Exception("Provide two different integer object UIDs.");
	WorldObjectRef target_guard=meshSnapshot(all,w,p,n,user,"replace_uid");
	WorldObjectRef operand_guard=new WorldObject();
	{
		WorldStateLock lock(all.mutex);WorldObjectRef source=findObject(w,lock,operand_id);
		if(!editable(*source,w,user)) throw glare::Exception("Permission denied for the operand object.");
		if(source->object_type!=WorldObject::ObjectType_Generic || !hasExtension(toStdString(source->model_url),"bmesh")) throw glare::Exception("Boolean operations require two editable native .bmesh objects.");
		operand_guard->copyNetworkStateFrom(*source);
	}
	const std::string operand_path=localMeshResourcePath(all,operand_guard->model_url);
	WorldObjectRef ob=new WorldObject();ob->copyNetworkStateFrom(*target_guard);
	if(FileUtils::getFileSize(operand_path)>16*1024*1024) throw glare::Exception("Operand mesh exceeds the 16 MiB MCP limit.");
	Reference<BatchedMesh> mesh_a=readNativeMesh(all,*target_guard),mesh_b=readNativeMesh(all,*operand_guard);
	Reference<Indigo::Mesh> native_a=mesh_a->buildIndigoMesh(),native_b=mesh_b->buildIndigoMesh();
	if(ob->materials.size()+operand_guard->materials.size()>255) throw glare::Exception("Combined materials exceed the 255-slot limit.");
	MCPMeshOps::Mesh a=operationsMesh(*native_a),b=operationsMesh(*native_b);
	// Boolean geometry is evaluated in target-local space. Convert the operand
	// through world space so independently positioned, rotated and scaled objects
	// can be joined without requiring users to align their editor transforms.
	const Matrix4f operand_to_target=target_guard->worldToObMatrix()*operand_guard->obToWorldMatrix();
	Matrix4f normal_to_target;float determinant=0.f;
	if(!operand_to_target.getUpperLeftInverseTranspose(normal_to_target) || !std::isfinite(determinant=operand_to_target.upperLeftDeterminant()) || std::fabs(determinant)<1.0e-10f)
		throw glare::Exception("Operand transform cannot be converted into target-local coordinates.");
	for(MCPMeshOps::Triangle& tri:b)
	{
		for(MCPMeshOps::Vertex& vertex:tri.vertices)
		{
			const Vec4f transformed_point=operand_to_target.mul3Point(Vec4f((float)vertex.position.x,(float)vertex.position.y,(float)vertex.position.z,1.f));
			const Vec4f transformed_normal=normal_to_target.mul3Vector(Vec4f((float)vertex.normal.x,(float)vertex.normal.y,(float)vertex.normal.z,0.f));
			if(!transformed_point.isFinite() || !transformed_normal.isFinite() || std::fabs(transformed_point.x[0])>1.0e7f || std::fabs(transformed_point.x[1])>1.0e7f || std::fabs(transformed_point.x[2])>1.0e7f)
				throw glare::Exception("Transformed operand geometry is outside supported world coordinates.");
			vertex.position=MCPMeshOps::Vec3(transformed_point.x[0],transformed_point.x[1],transformed_point.x[2]);
			vertex.normal=MCPMeshOps::unit(MCPMeshOps::Vec3(transformed_normal.x[0],transformed_normal.x[1],transformed_normal.x[2]));
		}
		if(determinant<0.f) std::swap(tri.vertices[1],tri.vertices[2]); // Mirrored scale reverses winding.
	}
	const unsigned offset=(unsigned)ob->materials.size();
	for(auto& tri:b) { if(tri.material>=operand_guard->materials.size()) throw glare::Exception("Operand has an invalid material index.");tri.material+=offset; }
	for(const auto& material:operand_guard->materials) ob->materials.push_back(material->clone());
	MCPMeshOps::Mesh result_ops;const std::string operation=n.getChildStringValue(p,"operation");
	try { result_ops=MCPMeshOps::booleanMesh(a,b,operation); } catch(const std::exception& e) { throw glare::Exception(e.what()); }
	if(result_ops.empty()) throw glare::Exception("Boolean result is empty; no object was changed.");
	MeshBuilder builder;builder.appendOperationsMesh(result_ops);builder.mesh.endOfModel();Reference<BatchedMesh> result=BatchedMesh::buildFromIndigoMesh(builder.mesh);result->checkValidAndSanitiseMesh();ob->setAABBOS(result->aabb_os);
	const std::vector<WorldObjectRef> sources{target_guard,operand_guard};
	return publish(all,w,ob,p,n,user,username,result,&sources);
}

static std::string getObject(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n)
{
	WorldStateLock lock(all.mutex);
	return describe(*findObject(w,lock,objectUID(p,n)));
}

static std::string getGeometry(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n)
{
	WorldObjectRef snapshot=new WorldObject();
	{
		WorldStateLock lock(all.mutex);
		snapshot->copyNetworkStateFrom(*findObject(w,lock,objectUID(p,n)));
	}
	const size_t offset=(size_t)integer(p,n,"offset",0,0,10000000),limit=(size_t)integer(p,n,"limit",500,1,2000);
	const std::string section=n.getChildStringValueWithDefaultVal(p,"section",snapshot->object_type==WorldObject::ObjectType_VoxelGroup ? "voxels" : "vertices");
	std::string out="{\"section\":\""+web::Escaping::JSONEscape(section)+"\",\"offset\":"+toString(offset)+",\"items\":[";
	size_t total=0,end=0;
	if(snapshot->object_type==WorldObject::ObjectType_VoxelGroup)
	{
		if(section!="voxels") throw glare::Exception("Voxel objects expose the voxels section.");
		snapshot->decompressVoxels();
		const auto& cells=snapshot->getDecompressedVoxels();total=cells.size();end=std::min(total,offset+limit);
		for(size_t i=offset;i<end;++i)
		{
			if(i!=offset) out+=",";
			out+="{\"x\":"+toString(cells[i].pos.x)+",\"y\":"+toString(cells[i].pos.y)+",\"z\":"+toString(cells[i].pos.z)+",\"material\":"+toString(cells[i].mat_index)+"}";
		}
	}
	else
	{
		if(section!="vertices" && section!="triangles") throw glare::Exception("Mesh objects expose vertices and triangles sections.");
		const std::string path=localMeshResourcePath(all,snapshot->model_url);
		if(FileUtils::getFileSize(path)>16*1024*1024) throw glare::Exception("Source mesh exceeds the 16 MiB MCP resource limit.");
		Reference<BatchedMesh> mesh=BatchedMesh::readFromFile(path,NULL);mesh->checkValidAndSanitiseMesh();
		Reference<Indigo::Mesh> native=mesh->buildIndigoMesh();
		total=section=="vertices" ? native->vert_positions.size() : native->triangles.size();end=std::min(total,offset+limit);
		for(size_t i=offset;i<end;++i)
		{
			if(i!=offset) out+=",";
			if(section=="vertices") { const auto& v=native->vert_positions[i];out+=vecJSON(Vec3d(v.x,v.y,v.z)); }
			else { const auto& t=native->triangles[i];out+="{\"indices\":["+toString(t.vertex_indices[0])+","+toString(t.vertex_indices[1])+","+toString(t.vertex_indices[2])+"],\"material\":"+toString(t.tri_mat_index)+"}"; }
		}
	}
	return out+"],\"total\":"+toString(total)+",\"next_offset\":"+(end<total ? toString(end) : "null")+"}";
}

static std::string validateMesh(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n)
{
	WorldObjectRef snapshot=new WorldObject();
	{
		WorldStateLock lock(all.mutex);snapshot->copyNetworkStateFrom(*findObject(w,lock,objectUID(p,n)));
	}
	if(snapshot->object_type==WorldObject::ObjectType_VoxelGroup) throw glare::Exception("validate_mesh checks native triangle meshes; voxel objects use get_geometry with section=voxels.");
	const std::string path=localMeshResourcePath(all,snapshot->model_url);
	if(FileUtils::getFileSize(path)>64*1024*1024) throw glare::Exception("Source mesh exceeds the 64 MiB MCP validation limit.");
	Reference<BatchedMesh> native=BatchedMesh::readFromFile(path,NULL);native->checkValidAndSanitiseMesh();
	Reference<Indigo::Mesh> source=native->buildIndigoMesh();const MCPMeshOps::Mesh mesh=operationsMesh(*source,MCPMeshOps::Detail::MAX_VALIDATION_TRIANGLES);
	const MCPMeshOps::ValidationReport r=MCPMeshOps::validateMesh(mesh);
	std::string boolean_issue;bool boolean_compatible=false;
	if(mesh.size()>MCPMeshOps::Detail::MAX_INPUT_TRIANGLES) boolean_issue="Boolean operations allow at most 4096 triangles per operand.";
	else try {
		const double eps=MCPMeshOps::Detail::tolerance(mesh);MCPMeshOps::validateClosed(mesh,eps);
		MCPMeshOps::Detail::Budget budget;MCPMeshOps::Detail::rejectSelfIntersections(mesh,eps,budget);MCPMeshOps::Detail::validateShellOrientations(mesh,eps,budget);
		boolean_compatible=true;
	} catch(const std::exception& e) { boolean_issue=e.what(); }
	return "{\"uid\":"+toString(snapshot->uid.value())+",\"triangles\":"+toString(r.triangles)+",\"welded_vertices\":"+toString(r.welded_vertices)+
		",\"connected_components\":"+toString(r.connected_components)+",\"closed_manifold\":"+std::string(r.closed_manifold?"true":"false")+
		",\"boundary_edges\":"+toString(r.boundary_edges)+",\"nonmanifold_edges\":"+toString(r.nonmanifold_edges)+
		",\"winding_consistent\":"+std::string(r.winding_consistent?"true":"false")+",\"inconsistent_winding_edges\":"+toString(r.inconsistent_winding_edges)+
		",\"positive_signed_volume\":"+std::string(r.signed_volume>0?"true":"false")+",\"signed_volume\":"+doubleToString(r.signed_volume)+
		",\"degenerate_triangles\":"+toString(r.degenerate_triangles)+",\"duplicate_triangles\":"+toString(r.duplicate_triangles)+
		",\"invalid_normals\":"+toString(r.invalid_normals)+",\"degenerate_uv_triangles\":"+toString(r.degenerate_uv_triangles)+
		",\"boolean_compatible\":"+std::string(boolean_compatible?"true":"false")+",\"boolean_issue\":\""+web::Escaping::JSONEscape(boolean_issue)+"\"}";
}

static std::string editObject(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string&,bool remove)
{
	WorldStateLock lock(all.mutex);
	if(all.isInReadOnlyMode()) throw glare::Exception("Server is in read-only mode.");
	WorldObjectRef ob=findObject(w,lock,objectUID(p,n));
	if(!editable(*ob,w,user)) throw glare::Exception("Permission denied for this object.");
	if(remove) { dirtyChunk(w,lock,*ob);ob->state=WorldObject::State_Dead; }
	else
	{
		WorldObjectRef candidate=new WorldObject();candidate->copyNetworkStateFrom(*ob);
		transform(p,n,*candidate);
		if(n.hasChild("materials")) materials(p,n,*candidate);
		validateTextures(all,*candidate);
		if(candidate->materials.size()!=ob->materials.size()) throw glare::Exception("Material editing must preserve the number of material slots. Rebuild geometry to change slots.");
		permission(*candidate,w,lock,user);
		dirtyChunk(w,lock,*ob);
		ob->copyNetworkStateFrom(*candidate);ob->transformChanged();dirtyChunk(w,lock,*ob);
	}
	ob->last_modified_time=TimeStamp::currentTime();ob->from_remote_other_dirty=true;
	w.addWorldObjectAsDBDirty(ob,lock);w.getDirtyFromRemoteObjects(lock).insert(ob);all.markAsChanged();
	return remove ? "{\"deleted_uid\":"+toString(ob->uid.value())+"}" : describe(*ob);
}

static std::string duplicateObject(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	WorldObjectRef ob=new WorldObject();
	{
		WorldStateLock lock(all.mutex);
		WorldObjectRef source=findObject(w,lock,objectUID(p,n));
		if(!editable(*source,w,user)) throw glare::Exception("You can only duplicate objects you can edit.");
		ob->copyNetworkStateFrom(*source);
	}
	const size_t slots=ob->materials.size();
	if(n.hasChild("materials")) materials(p,n,*ob);
	if(slots!=ob->materials.size()) throw glare::Exception("Duplicating an object must preserve its material slot count.");
	if(n.hasChild("replace_uid")) throw glare::Exception("duplicate_object cannot replace an object.");
	return publish(all,w,ob,p,n,user,username,Reference<BatchedMesh>());
}

static std::string cube(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	WorldObjectRef ob=new WorldObject();ob->pos=Vec3d(0,0,0);ob->object_type=WorldObject::ObjectType_Generic;ob->axis=Vec3f(0,0,1);ob->angle=0;ob->scale=Vec3f(1,1,1);
	materials(p,n,*ob);
	const Vec3d c=vector(p,n,"color",Vec3d(.65,.65,.65),0,1);
	if(n.hasChild("color")) ob->materials[0]->colour_rgb=Colour3f((float)c.x,(float)c.y,(float)c.z);
	ob->materials[0]->roughness.val=(float)number(p,n,"roughness",ob->materials[0]->roughness.val,0,1);
	ob->materials[0]->metallic_fraction.val=(float)number(p,n,"metallic",ob->materials[0]->metallic_fraction.val,0,1);
	MeshBuilder b;b.boxPart(p,n,false,(int)ob->materials.size());b.mesh.endOfModel();
	Reference<BatchedMesh> mesh=BatchedMesh::buildFromIndigoMesh(b.mesh);ob->setAABBOS(mesh->aabb_os);
	return publish(all,w,ob,p,n,user,username,mesh);
}

static std::string imageObject(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	WorldObjectRef ob=new WorldObject(); ob->object_type=WorldObject::ObjectType_Generic;
	ob->pos=Vec3d(0,0,0); ob->scale=Vec3f(1,1,1); ob->axis=Vec3f(0,0,1); ob->angle=0;
	WorldMaterialRef m=new WorldMaterial(); m->colour_rgb=Colour3f(1,1,1);
	m->colour_texture_url=textureURL(p,n,"texture_url");
	if(m->colour_texture_url.empty()) throw glare::Exception("texture_url is required.");
	m->flags |= WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG;
	if(n.getChildBoolValueWithDefaultVal(p,"double_sided",false)) m->flags |= WorldMaterial::DOUBLE_SIDED_FLAG;
	const double width=number(p,n,"width",2,.01,100),height=number(p,n,"height",2,.01,100);
	m->tex_matrix=Matrix2f(1,0,0,1);
	ob->materials.push_back(m);
	MeshBuilder b; b.quad(Vec3d(0,0,0),Vec3d(width,0,0),Vec3d(width,0,height),Vec3d(0,0,height));
	// Image fitted once, independent of physical dimensions or later scaling.
	const double uv[2][6]={{0,0,1,0,1,1},{0,0,1,1,0,1}};
	for(int i=0;i<2;++i) for(int k=0;k<3;++k) b.mesh.uv_pairs[b.mesh.triangles[i].uv_indices[k]]=Indigo::Vec2f((float)uv[i][2*k],(float)uv[i][2*k+1]);
	b.mesh.endOfModel(); Reference<BatchedMesh> mesh=BatchedMesh::buildFromIndigoMesh(b.mesh); ob->setAABBOS(mesh->aabb_os);
	return publish(all,w,ob,p,n,user,username,mesh);
}

// Replace the UVs and material only on one axis-aligned face of a native mesh.
// This keeps the image on a single square and leaves the other faces/materials intact.
static std::string applyImageTexture(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	const double uid_value=number(p,n,"replace_uid",0,0,9007199254740991.0);
	if(uid_value!=std::floor(uid_value)) throw glare::Exception("replace_uid must be an integer.");
	const UID uid((uint64)uid_value);
	const std::string face=n.getChildStringValue(p,"face");
	const URLString texture=textureURL(p,n,"texture_url");
	if(texture.empty()) throw glare::Exception("texture_url is required.");
	if(face!="-X" && face!="+X" && face!="-Y" && face!="+Y" && face!="-Z" && face!="+Z") throw glare::Exception("Choose one face: -X, +X, -Y, +Y, -Z or +Z.");
	WorldObjectRef snapshot=new WorldObject();
	{
		WorldStateLock lock(all.mutex);
		const auto found=w.getObjects(lock).find(uid);
		if(found==w.getObjects(lock).end() || found->second->state==WorldObject::State_Dead) throw glare::Exception("Object not found in the active world.");
		WorldObjectRef source=found->second;
		if(!editable(*source,w,user)) throw glare::Exception("Permission denied for this object.");
		if(source->object_type!=WorldObject::ObjectType_Generic || !hasExtension(toStdString(source->model_url),"bmesh") || !all.resource_manager->isFileForURLPresent(source->model_url))
			throw glare::Exception("Image face mapping requires an editable native .bmesh model.");
		snapshot->copyNetworkStateFrom(*source);
	}
	if(FileUtils::getFileSize(all.resource_manager->pathForURL(snapshot->model_url))>16*1024*1024) throw glare::Exception("Source mesh exceeds the 16 MiB image-mapping limit.");
	WorldObjectRef source_guard=new WorldObject();source_guard->copyNetworkStateFrom(*snapshot);
	Reference<BatchedMesh> source_mesh=BatchedMesh::readFromFile(all.resource_manager->pathForURL(snapshot->model_url),NULL);
	source_mesh->checkValidAndSanitiseMesh();
	Reference<Indigo::Mesh> native=source_mesh->buildIndigoMesh();
	if(snapshot->materials.size()>=255) throw glare::Exception("The object already has the maximum number of material slots.");
	double lo[3]={1e30,1e30,1e30},hi[3]={-1e30,-1e30,-1e30};
	for(const auto& v:native->vert_positions) for(int k=0;k<3;++k) { lo[k]=myMin(lo[k],(double)v[k]);hi[k]=myMax(hi[k],(double)v[k]); }
	int axis=(face[1]=='X')?0:(face[1]=='Y')?1:2;const int sign=face[0]=='+'?1:-1;
	int uv0=(axis+1)%3,uv1=(axis+2)%3;
	unsigned int selected=0;
	for(auto& tri:native->triangles)
	{
		const auto& a=native->vert_positions[tri.vertex_indices[0]];const auto& b=native->vert_positions[tri.vertex_indices[1]];const auto& c=native->vert_positions[tri.vertex_indices[2]];
		const double ab[3]={(double)b.x-a.x,(double)b.y-a.y,(double)b.z-a.z},ac[3]={(double)c.x-a.x,(double)c.y-a.y,(double)c.z-a.z};
		const double normal[3]={ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]};
		const int dominant=std::abs(normal[0])>=std::abs(normal[1]) && std::abs(normal[0])>=std::abs(normal[2]) ? 0 : std::abs(normal[1])>=std::abs(normal[2]) ? 1 : 2;
		if(dominant!=axis || normal[axis]*sign<=0) continue;
		if(tri.uv_indices[0]>=native->uv_pairs.size() || tri.uv_indices[1]>=native->uv_pairs.size() || tri.uv_indices[2]>=native->uv_pairs.size()) throw glare::Exception("Source mesh has invalid UV indices.");
		for(int k=0;k<3;++k)
		{
			const auto& v=native->vert_positions[tri.vertex_indices[k]];
			const double d0=hi[uv0]-lo[uv0],d1=hi[uv1]-lo[uv1];
			if(d0<1e-8 || d1<1e-8) throw glare::Exception("Selected face has zero area.");
			const double p0=uv0==0?v.x:uv0==1?v.y:v.z,p1=uv1==0?v.x:uv1==1?v.y:v.z;
			native->uv_pairs[tri.uv_indices[k]]=Indigo::Vec2f((float)((p0-lo[uv0])/d0),(float)((p1-lo[uv1])/d1));
		}
		tri.tri_mat_index=(uint32)snapshot->materials.size();++selected;
	}
	if(selected==0) throw glare::Exception("No triangles matched that object-local face. Use an axis-aligned cube or square face.");
	WorldMaterialRef material=snapshot->materials[0]->clone();
	material->colour_texture_url=texture;material->colour_rgb=Colour3f(1,1,1);material->flags|=WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG;
	snapshot->materials.push_back(material);
	native->endOfModel();
	Reference<BatchedMesh> result=BatchedMesh::buildFromIndigoMesh(*native);result->checkValidAndSanitiseMesh();snapshot->setAABBOS(result->aabb_os);
	const std::vector<WorldObjectRef> sources{source_guard};
	return publish(all,w,snapshot,p,n,user,username,result,&sources);
}

static std::string resourceObject(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	const URLString url=toURLString(n.getChildStringValue(p,"model_url"));
	if(!hasExtension(toStdString(url),"bmesh") || !all.resource_manager->isFileForURLPresent(url)) throw glare::Exception("Use an existing local .bmesh model from list_resources.");
	if(FileUtils::getFileSize(all.resource_manager->pathForURL(url))>16*1024*1024) throw glare::Exception("Source model exceeds the 16 MiB MCP resource limit.");
	Reference<BatchedMesh> mesh=BatchedMesh::readFromFile(all.resource_manager->pathForURL(url),NULL);
	mesh->checkValidAndSanitiseMesh();
	WorldObjectRef ob=new WorldObject();ob->pos=Vec3d(0,0,0);ob->object_type=WorldObject::ObjectType_Generic;ob->model_url=url;ob->axis=Vec3f(0,0,1);ob->angle=0;ob->scale=Vec3f(1,1,1);
	materials(p,n,*ob);
	for(const auto& batch:mesh->batches) if(batch.material_index>=ob->materials.size()) throw glare::Exception("Provide a material for each slot in the source mesh.");
	ob->setAABBOS(mesh->aabb_os);
	return publish(all,w,ob,p,n,user,username,Reference<BatchedMesh>());
}

static std::string capabilities()
{
	return R"json({"application":"Metasiberia","coordinates":{"up":"+Z","unit":"metre","parts":"local coordinates in one mesh; centered unit primitives","rotation":"parts use XYZ degrees; objects use axis plus angle in radians","placement":"Omitting pos places the object origin 3 metres in front of your avatar at eye height. Inspect list_avatars and use explicit pos for ground placement; terrain elevation is not inferred."},"workflow":["Use get_modeling_example for mesh_robot, mesh_bench, mesh_vase, mesh_arch, mesh_kiosk, mesh_road, voxel_cottage or voxel_tower recipes, then adapt proportions and details to the user request.","Read get_world_info, list_avatars, list_objects_near before a substantial build. Choose a clear footprint and explicit origin.","For spatial orientation, treat all object pos/bounds and avatar coordinates as world-space metres with +Z up. Query list_objects_in_bounds around the full requested route/area (paginate next_offset), then get_object and get_geometry for anchor UIDs; get_geometry vertices are object-local, so apply each object transform before comparing them with other objects or world coordinates. Use list_objects_near for an avatar-centred first pass, not as a complete scene inventory.","Before any creation or enlarged/moved replacement, call check_build_permissions with the complete planned world AABB. If denied, explain in the user language: you cannot build here; use your own/writable parcel, an authorised sandbox area, or your personal world. Do not pretend success, bypass checks, or automatically move the model elsewhere.","Plan silhouette, dimensions, function and a small material palette. Build large masses, then medium structure, then small details. Use intentional symmetry, spacing, thickness and supports. If any modeling call fails, read its error, correct the plan or split it into coherent nearby components, retry and continue the remaining work; do not abandon the request or report partial work as complete. If a permission or hard limit blocks progress, state the exact blocker and what remains unfinished.","A cube is a native editable mesh building block. For box-based design use box parts with independent sizes and per-face material slots, deformed_box with 8 corner positions for tapered/sloped forms, or indexed vertices/triangles for custom topology. Combine deliberate structural parts into one multi-material mesh; material slots remain independent. A complex model is not a single solid cube.","Separate functional surfaces into material slots: walls/frame/glass/metal/wood/trim. box face_materials order is -Z,+Z,-Y,+X,+Y,-X. Reuse slots for the same substance; use different slots when the user needs independent editing. Carve openings instead of covering solid interiors.","For curved roads, bridges and causeways use one shape=road deck with a local path, width, thickness, smooth=true and samples_per_span=8..16. It rounds bends smoothly through the control anchors; do not approximate a bend using separate straight boxes. Keep path points local to the object and put world placement in pos. For curved pipes, cables, handrails and branches use shape=tube with a local 3D centerline path, tube_radius, segments and optional closed=true. Use 2..128 distinct points; avoid consecutive duplicates and near-180-degree reversals. Swept tubes use smooth transported frames, arc-length UVs and end caps when open.","For smooth architecture or furniture use build_mesh parts with boxes, rounded_box, beveled_box (chamfered box only), spheres, cylinders, cones, toruses, quad, capsule, icosahedron, platonic_solid (library dodecahedron), pyramid, octahedron, wedge, triangular_prism, hexagonal_prism, lathe profiles and extruded simple contours. repeat/step/rotation_step generate repeated details within one mesh. They become one native mesh object with per-part materials. Avoid one network object per brick.","For true openings in a solid, create a closed base part first, then add cutter parts with operation=subtract (or union/intersect) in the same build_mesh recipe. Each Boolean operand is limited to 4096 triangles and must be closed, manifold and consistently oriented; open overlapping assemblies are not Boolean operands. boolean_mesh also accepts separate objects with different positions, rotations and scales and transforms the operand into the target local frame automatically. To permanently join two closed mesh segments, place them with a deliberate small overlap, call boolean_mesh(operation=union), then inspect the result and bounds; merely touching coplanar end faces can still leave a visible seam. Use validate_mesh to inspect topology, winding, normals, UVs and Boolean compatibility.","For a hole through an extruded XY contour, use shape=extrude with holes=[contour,...]; each simple inner contour is cut through the full depth and must lie strictly inside the outer contour without touching other contours.","For existing native meshes call get_geometry first, then edit_mesh_topology. Extrusion/inset uses triangle face indices for one connected planar region. Edge bevel uses selected local-space endpoint pairs from get_geometry and currently requires a convex closed mesh of at most 4096 triangles; adjacent edges are allowed. set_face_material assigns an existing material slot to selected triangles. Use unwrap_mesh_uv to generate a packed atlas. Read back the same UID and call validate_mesh after each topology edit.","For rotational designs (vases, columns, turned legs), use lathe with [radius,z] profile. For moldings, arches, brackets use extrude with a simple XY contour and depth. rounded_box gives softer edges. For custom shapes use indexed triangles with counter-clockwise winding from outside. Primitives have smooth curved surfaces and sharp caps/box edges; custom triangles use flat geometric normals.","For object scripting, call get_luau_reference with overview and the relevant topic before writing code. Read a target object with get_object_script using its exact UID; write with update_object_script only after reviewing existing logic and permission. Use documented Luau APIs, start the script with --lua, use this_object.uid for its UID, and reread the exact UID after saving. Scripts execute on clients and, when enabled, on the server; the tool reloads the server evaluator and marks clients to reload.","For world terrain editing, first call inspect_terrain to read section bounds and heightmap/mask layers. Use screenshot plus render_view to locate the requested flat edge, convert it to absolute world x/y coordinates, then call smooth_terrain with a radius covering the edge and a soft transition. This edits only the loaded heightmap EXR, preserves vegetation/road/building masks and detail textures, requires world-owner/god permission, and reports failure when the target section is not loaded; do not claim completion without a successful tool result.","For an attached blueprint or technical drawing, classify its views and read units, explicit dimensions, datums and notes before modeling. Convert mm/cm to metres; map front to X/Z, side to Y/Z and top to X/Y. Use explicit dimensions over pixel estimates, reconcile repeated dimensions across views, and state assumptions for unlabeled or hidden geometry. If views conflict or an essential dimension is missing, ask about that specific issue before committing to a precise build.","For voxel art use create_voxel_object with ordered add/subtract/paint box, ellipsoid, cylinder, cone, torus and wedge volumes. axis rotates axial shapes; shell hollows volumes by a bounding-box inset. Use 0.05-0.2 metre cells for furniture/details, coarser cells for large structures. Carve openings and interiors, paint accents, avoid solid filled buildings.","Read every returned UID using get_object and compare bounds/materials with the design. Call validate_mesh for every custom mesh up to 250000 triangles and review its manifold, winding, normal, UV and Boolean-preflight report; diagnostics do not increase the 4096-triangle Boolean operand limit. Server existence does not prove visual quality or client rendering. Use the local render_view tool (if listed) for at least perspective and side/front images after loading; compare silhouette/proportions/materials against attached references and revise the same UID. It sees loaded geometry near the avatar, not distant unloaded assets. Never claim visual inspection without a render tool result.","Refine using build_mesh/create_voxel_object replace_uid to replace geometry while retaining identity, or update_object for transform/material changes. Keep your construction recipe in conversation. User editor undo history is not shared with MCP.","Voxel quality comes from silhouette, palette, planned cell size, hollow volumes, subtractive openings, layered construction and controlled detail. Use coarse structure and finer accents, inspect from multiple views, retain the full recipe and refine the same UID."],"mcp_supported":["world/avatar/object inspection including paginated world-space AABB queries","native mesh assembly from primitives","custom indexed triangle meshes","blueprint-guided modeling from attached dimensioned drawings","smooth Catmull-Rom roadway decks and path-swept tube meshes for curved parts","ordered inline Boolean operations in build_mesh","extruded profiles with through-hole contours","planar face-region extrusion and inset on existing meshes","selected-face material assignment","packed UV atlas generation with preserved secondary UVs","selected-edge bevel on convex closed meshes including adjacent edges","read-only mesh topology, winding, normal and UV diagnostics","voxel constructive modeling and palette","native model resource instancing","move/rotate/scale/material editing","duplicate and delete with permissions","read-only build permission preflight","cube corner deformation and per-face material slots","bounded bevel and vertex deformation of native meshes","single-face image projection with independent UVs","bounded union/subtract/intersect of closed meshes across different world transforms, consuming the second operand","PBR texture maps and UV scale","local procedural textures and vector images through Qt MCP","Poly Haven CC0 textures through Qt MCP when enabled","image resource import and world image planes","world terrain section inspection and bounded EXR heightmap smoothing through the local client bridge","read internal Luau API reference and examples","read and update an owned object script by UID, with evaluator reload and client notification"],"editors":[{"name":"ObjectEditor","mcp":"transform, materials and content via update_object; Luau scripts via get_object_script/update_object_script; physics and media settings not exposed"},{"name":"MaterialEditor","mcp":"color, roughness, metallic, opacity and emissive values; color/normal/packed metallic-roughness/emission PNG textures, UV scale, double-sided materials and generated UV atlas via MCP"},{"name":"VoxelEditorPanel","mcp":"native voxel geometry via create_voxel_object; interactive brushes/layers/history not exposed"},{"name":"ParcelEditor","mcp":"write permissions enforced, parcel editing not exposed"},{"name":"AnimationEditorPanel","mcp":"not exposed"},{"name":"BotEditorWidget","mcp":"not exposed"},{"name":"TreeEditorPanel","mcp":"not exposed; geometric trees can be built from primitives or voxels"},{"name":"WorldSettingsTerrainSculpting","mcp":"inspect_terrain and smooth_terrain edit loaded heightmap EXR sections through the local client bridge; other terrain layers are preserved"},{"name":"ScientificObjectEditor","mcp":"not exposed"},{"name":"CulturalObjectEditor","mcp":"not exposed"},{"name":"DocumentEditorPanel","mcp":"not exposed"},{"name":"GearEditorUI","mcp":"not exposed"},{"name":"ShaderEditorDialog","mcp":"not exposed"},{"name":"SpotlightEditor","mcp":"not exposed"}],"limits":{"request_bytes":8388608,"primitive_parts":512,"mesh_triangles":100000,"voxel_cells":262144,"voxel_axis_cells":256,"voxel_candidate_cells":4000000},"not_supported":["arbitrary sculpting, subdivision and remeshing; selected-edge bevel requires one convex closed mesh","interactive UV island editing and manual seam placement; MCP can generate a packed UV atlas","rigging and animation (planned for later)","persistent parametric build recipes, shared editor undo/redo and remote manipulation of every Qt editor","automatic proof of visual quality"],"project":{"name":"Metasiberia","creator":"Денис Шипилов","knowledge_policy":"Creator name supplied by the project owner. Do not invent biography, contact details, history or features not in current capabilities."}})json";
}

// The schemas are maintained separately from implementation for readable tool discovery.
#include "MCPModelingSchemas.h"
#include "MCPModelingExamples.h"

} } // namespace MCPHandlers::Modeling
