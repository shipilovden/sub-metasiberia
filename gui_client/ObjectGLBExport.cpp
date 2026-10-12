#include "ObjectGLBExport.h"
#if !defined(USE_SDL)
#include "GUIClient.h"
#include "WorldState.h"
#include "../shared/ResourceManager.h"
#include "../shared/WorldObject.h"
#include "../shared/WorldMaterial.h"
#include "../shared/ObjectCopyPermissions.h"
#include "../shared/VoxelMeshBuilding.h"
#include <graphics/BatchedMesh.h>
#include <graphics/SRGBUtils.h>
#include <utils/FileUtils.h>
#include <utils/StringUtils.h>
#include <utils/LimitedAllocator.h>
#include <QtCore/QBuffer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtGui/QImageReader>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QSaveFile>
#include <QtCore/QThread>
#include <QtCore/QtEndian>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace ObjectGLBExport { namespace {
constexpr qint64 MAX_BINARY=256LL*1024*1024;
constexpr size_t MAX_CORNERS=1500000;
using V=std::array<double,3>;
static void fail(const QString& message) { throw glare::Exception(message.toUtf8().constData()); }
static void finite(double x) { if(!std::isfinite(x)||std::abs(x)>1e12) fail(QStringLiteral("GLB: non-finite or excessive numeric value.")); }
static QJsonArray array(const V& v) { for(double x:v) finite(x);return QJsonArray{v[0],v[1],v[2]}; }
static V minus(const V& a,const V& b) { return {{a[0]-b[0],a[1]-b[1],a[2]-b[2]}}; }
static V cross(const V& a,const V& b) { return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}}; }
static double dot(const V& a,const V& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static V unit(V v) { const double length=std::sqrt(dot(v,v));if(length<1e-12) fail(QStringLiteral("GLB: degenerate normal or tangent."));for(double& x:v) x/=length;return v; }
static double halfValue(const uint8* data) { uint16 x;std::memcpy(&x,data,2);const int exponent=(x>>10)&31,mantissa=x&1023;if(exponent==31) fail(QStringLiteral("GLB: non-finite half attribute."));const double v=exponent?std::ldexp(1+mantissa/1024.0,exponent-15):std::ldexp((double)mantissa,-24);return x&0x8000?-v:v; }
static double component(const uint8* data,BatchedMesh::ComponentType type,int c,bool normalized=false) {
	double value=0;
	if(type==BatchedMesh::ComponentType_Float) { float f;std::memcpy(&f,data+c*4,4);value=f; }
	else if(type==BatchedMesh::ComponentType_Half) value=halfValue(data+c*2);
	else if(type==BatchedMesh::ComponentType_UInt8) value=data[c]/(normalized?255.0:1.0);
	else if(type==BatchedMesh::ComponentType_UInt16) { uint16 q;std::memcpy(&q,data+c*2,2);value=q/(normalized?65535.0:1.0); }
	else if(type==BatchedMesh::ComponentType_UInt32) { uint32 q;std::memcpy(&q,data+c*4,4);value=q; }
	else fail(QStringLiteral("GLB: unsupported vertex component format."));finite(value);return value;
}
static V position(const BatchedMesh& mesh,size_t index) {
	const auto& a=mesh.getAttribute(BatchedMesh::VertAttribute_Position);const uint8* data=mesh.vertex_data.data()+index*mesh.vertexSize()+a.offset_B;V p;
	if(a.component_type!=BatchedMesh::ComponentType_Float&&a.component_type!=BatchedMesh::ComponentType_Half&&a.component_type!=BatchedMesh::ComponentType_UInt16) fail(QStringLiteral("GLB: unsupported position format."));
	for(int c=0;c<3;++c) { p[c]=component(data,a.component_type,c);if(a.component_type==BatchedMesh::ComponentType_UInt16) p[c]=mesh.aabb_os.min_[c]+p[c]/65535.0*(mesh.aabb_os.max_[c]-mesh.aabb_os.min_[c]);finite(p[c]); }return p;
}
static std::array<double,4> normal(const BatchedMesh& mesh,size_t index,const BatchedMesh::VertAttribute& a,bool tangent=false) {
	const uint8* data=mesh.vertex_data.data()+index*mesh.vertexSize()+a.offset_B;std::array<double,4> out{{0,0,0,1}};
	if(a.component_type==BatchedMesh::ComponentType_PackedNormal) { uint32 q;std::memcpy(&q,data,4);const Vec4f n=tangent?batchedMeshUnpackNormalWithW(q):batchedMeshUnpackNormal(q);for(int c=0;c<4;++c) out[c]=n[c]; }
	else if(a.component_type==BatchedMesh::ComponentType_Oct16&&!tangent) { uint16 q;std::memcpy(&q,data,2);const double x=std::max(-1.0,(int8)(q>>8)/127.0),y=std::max(-1.0,(int8)(q&255)/127.0);out={{x,y,1-std::abs(x)-std::abs(y),1}};if(out[2]<0) { out[0]=(1-std::abs(y))*(x>=0?1:-1);out[1]=(1-std::abs(x))*(y>=0?1:-1); } }
	else { if(a.component_type!=BatchedMesh::ComponentType_Float&&a.component_type!=BatchedMesh::ComponentType_Half) fail(QStringLiteral("GLB: unsupported normal/tangent format."));for(int c=0;c<(tangent?4:3);++c) out[c]=component(data,a.component_type,c); }
	const V n=unit({{out[0],out[1],out[2]}});for(int c=0;c<3;++c) out[c]=n[c];if(tangent) out[3]=out[3]<0?-1:1;return out;
}

struct Builder {
	QByteArray bin;QJsonArray views,accessors,images,textures,materials;std::map<std::string,int> texture_cache;std::set<QString> extensions;
	void reserveBytes(qint64 bytes) { if(bytes<0||bin.size()+bytes>MAX_BINARY) fail(QStringLiteral("GLB exceeds the 256 MiB export limit.")); }
	int view(const QByteArray& data,int target=0) { while(bin.size()%4) bin.append('\0');reserveBytes(data.size());QJsonObject v{{"buffer",0},{"byteOffset",bin.size()},{"byteLength",data.size()}};if(target) v["target"]=target;const int i=views.size();views.append(v);bin.append(data);return i; }
	int accessor(const QByteArray& data,int count,const char* type,int components,int component_type=5126,int target=34962,bool bounds=false) {
		if(count<=0) fail(QStringLiteral("GLB: empty accessor."));QJsonObject a{{"bufferView",view(data,target)},{"componentType",component_type},{"count",count},{"type",type}};
		if(bounds) { QJsonArray lo,hi;for(int c=0;c<components;++c) { float mn=0,mx=0;for(int i=0;i<count;++i) { float f;std::memcpy(&f,data.constData()+(i*components+c)*4,4);if(!i||f<mn) mn=f;if(!i||f>mx) mx=f; }lo.append(mn);hi.append(mx); }a["min"]=lo;a["max"]=hi; }
		const int i=accessors.size();accessors.append(a);return i;
	}
	static void f32(QByteArray& data,double value) { finite(value);float f=(float)value;const int old=data.size();data.resize(old+4);std::memcpy(data.data()+old,&f,4); }
	static void u16(QByteArray& data,uint16 value) { const quint16 q=qToLittleEndian((quint16)value);data.append((const char*)&q,2); }
	int texture(const URLString& url,const std::map<std::string,std::string>& paths) {
		const std::string key=toStdString(url);const auto found=texture_cache.find(key);if(found!=texture_cache.end()) return found->second;
		const auto path=paths.find(key);if(path==paths.end()) fail(QStringLiteral("GLB: missing original texture resource."));const QString filename=QString::fromUtf8(path->second.c_str());
		QFile file(filename);if(!file.open(QIODevice::ReadOnly)||file.size()>32*1024*1024) fail(QStringLiteral("Cannot read texture or texture exceeds 32 MiB: ")+filename);
		QImageReader reader(filename);const QSize size=reader.size();if(!size.isValid()||(qint64)size.width()*size.height()>32*1024*1024) fail(QStringLiteral("Unsupported texture format or image exceeds 32 megapixels: ")+filename);
		QByteArray bytes;QString mime;const QByteArray format=reader.format().toLower();
		if(format=="png"||format=="jpeg"||format=="jpg") { bytes=file.readAll();mime=format=="png"?"image/png":"image/jpeg"; }
		else { const QImage image=reader.read();if(image.isNull()) fail(QStringLiteral("Texture decoding failed: ")+filename);QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);if(!image.save(&buffer,"PNG")) fail(QStringLiteral("Texture PNG conversion failed: ")+filename);mime="image/png"; }
		const int image=images.size();images.append(QJsonObject{{"bufferView",view(bytes)},{"mimeType",mime}});const int texture=textures.size();textures.append(QJsonObject{{"source",image},{"sampler",0}});texture_cache[key]=texture;return texture;
	}
};

static QJsonArray matrixJSON(const Matrix4f& matrix) { QJsonArray values;for(int c=0;c<4;++c) for(int r=0;r<4;++r) { const double value=matrix.elem(r,c);finite(value);values.append(value); }return values; }
static void addMaterials(Builder& b,const WorldObject& ob,const std::map<std::string,std::string>& paths) {
	for(const auto& ref:ob.materials) {
		if(ref.isNull()) fail(QStringLiteral("GLB: missing material slot."));const WorldMaterial& m=*ref;
		if(m.flags&(WorldMaterial::HOLOGRAM_FLAG|WorldMaterial::USE_VERT_COLOURS_FOR_WIND|WorldMaterial::DECAL_FLAG)) fail(QStringLiteral("Hologram, wind and decal shaders cannot be represented faithfully in GLB."));
		if(!m.opacity.texture_url.empty()||(!m.metallic_fraction.texture_url.empty()&&m.metallic_fraction.texture_url!=m.roughness.texture_url)) fail(QStringLiteral("Separate opacity/metallic texture maps are not supported by this object's metallic-roughness export."));
		for(double value:{(double)m.roughness.val,(double)m.metallic_fraction.val,(double)m.opacity.val}) if(!std::isfinite(value)||value<0||value>1) fail(QStringLiteral("GLB material factors must be within 0..1."));
		const Colour3f colour=toLinearSRGB(m.colour_rgb);QJsonObject pbr{{"baseColorFactor",QJsonArray{colour.r,colour.g,colour.b,m.opacity.val}},{"metallicFactor",m.metallic_fraction.val},{"roughnessFactor",m.roughness.val}};
		if(!m.colour_texture_url.empty()) pbr["baseColorTexture"]=QJsonObject{{"index",b.texture(m.colour_texture_url,paths)}};
		if(!m.roughness.texture_url.empty()) pbr["metallicRoughnessTexture"]=QJsonObject{{"index",b.texture(m.roughness.texture_url,paths)}};
		QJsonObject out{{"name",QString::fromUtf8(m.name.c_str())},{"pbrMetallicRoughness",pbr},{"doubleSided",(bool)(m.flags&WorldMaterial::DOUBLE_SIDED_FLAG)}};
		if(m.opacity.val<1||m.colourTexHasAlpha()) out["alphaMode"]="BLEND";
		if(!m.normal_map_url.empty()) out["normalTexture"]=QJsonObject{{"index",b.texture(m.normal_map_url,paths)}};
		if(!m.emission_texture_url.empty()) out["emissiveTexture"]=QJsonObject{{"index",b.texture(m.emission_texture_url,paths)}};
		const Colour3f emission=toLinearSRGB(m.emission_rgb);const double emission_scale=m.emission_lum_flux_or_lum*(1.0e-9/(683.002*106.856e-9));finite(emission_scale);if(emission_scale<0) fail(QStringLiteral("GLB: negative emissive luminance."));
		out["emissiveFactor"]=QJsonArray{emission.r,emission.g,emission.b};out["extensions"]=QJsonObject{{"KHR_materials_emissive_strength",QJsonObject{{"emissiveStrength",emission_scale}}}};b.extensions.insert("KHR_materials_emissive_strength");
		b.materials.append(out);
	}
}

static QJsonArray geometry(Builder& b,const BatchedMesh& mesh,const WorldObject& ob) {
	if(mesh.numIndices()==0||mesh.numIndices()%3||mesh.numIndices()>MAX_CORNERS||mesh.numVerts()>1000000) fail(QStringLiteral("GLB export supports up to 500000 triangles and 1000000 source vertices."));
	if(mesh.findAttribute(BatchedMesh::VertAttribute_MatIndex)) fail(QStringLiteral("Combined LOD meshes with per-vertex material indices must be exported from their original objects."));
	const auto* normals=mesh.findAttribute(BatchedMesh::VertAttribute_Normal);const auto* tangents=mesh.findAttribute(BatchedMesh::VertAttribute_Tangent);const auto* uv0=mesh.findAttribute(BatchedMesh::VertAttribute_UV_0);const auto* uv1=mesh.findAttribute(BatchedMesh::VertAttribute_UV_1);const auto* colours=mesh.findAttribute(BatchedMesh::VertAttribute_Colour);const auto* joints=mesh.findAttribute(BatchedMesh::VertAttribute_Joints);const auto* weights=mesh.findAttribute(BatchedMesh::VertAttribute_Weights);
	if((joints!=nullptr)!=(weights!=nullptr)) fail(QStringLiteral("GLB: incomplete skin attributes."));
	QJsonArray primitives;std::vector<bool> covered(mesh.numIndices()/3,false);
	for(const auto& batch:mesh.batches) {
		if(!batch.num_indices) continue;if(batch.indices_start%3||batch.num_indices%3||batch.indices_start>mesh.numIndices()||batch.num_indices>mesh.numIndices()-batch.indices_start||batch.material_index>=ob.materials.size()) fail(QStringLiteral("GLB: invalid material batch."));
		const auto& material=*ob.materials[batch.material_index];const Matrix2f texture_matrix=Matrix2f(1,0,0,-1)*material.tex_matrix;
		if(!uv0&&(!material.colour_texture_url.empty()||!material.roughness.texture_url.empty()||!material.normal_map_url.empty()||!material.emission_texture_url.empty())) fail(QStringLiteral("Textured object has no UV0 coordinates."));
		std::array<QByteArray,8> data; // positions, normals, UV0, UV1, colours, joints, weights, tangents
		for(size_t corner=batch.indices_start;corner<(size_t)batch.indices_start+batch.num_indices;++corner) {
			if(corner%3==0) { if(covered[corner/3]) fail(QStringLiteral("GLB: overlapping material batches."));covered[corner/3]=true; }
			const size_t vi=mesh.getIndexAsUInt32(corner);if(vi>=mesh.numVerts()) fail(QStringLiteral("GLB: invalid mesh index."));const V p=position(mesh,vi);for(double v:p) Builder::f32(data[0],v);
			V n;if(normals) { const auto decoded=normal(mesh,vi,*normals);n={{decoded[0],decoded[1],decoded[2]}}; }
			else { const size_t first=corner-corner%3;n=unit(cross(minus(position(mesh,mesh.getIndexAsUInt32(first+1)),position(mesh,mesh.getIndexAsUInt32(first))),minus(position(mesh,mesh.getIndexAsUInt32(first+2)),position(mesh,mesh.getIndexAsUInt32(first))))); }
			for(double v:n) Builder::f32(data[1],v);const uint8* base=mesh.vertex_data.data()+vi*mesh.vertexSize();
			for(int set=0;set<2;++set) { const auto* a=set?uv1:uv0;if(a) { double uv[2];for(int c=0;c<2;++c) { uv[c]=component(base+a->offset_B,a->component_type,c);if(a->component_type==BatchedMesh::ComponentType_UInt16) uv[c]*=set?mesh.uv1_scale:mesh.uv0_scale; }if(set==0) { const Vec2f v=texture_matrix*Vec2f((float)uv[0],(float)uv[1]);uv[0]=v.x;uv[1]=v.y; }else uv[1]=-uv[1];for(double v:uv) Builder::f32(data[set+2],v); } }
			if(colours) for(int c=0;c<3;++c) Builder::f32(data[4],component(base+colours->offset_B,colours->component_type,c,true));
			if(joints) { double sum=0;double values[4];for(int c=0;c<4;++c) { const double joint=component(base+joints->offset_B,joints->component_type,c);if(joint<0||joint>=mesh.animation_data.joint_nodes.size()||joint!=std::floor(joint)||joint>65535) fail(QStringLiteral("GLB: invalid skin joint."));Builder::u16(data[5],(uint16)joint);values[c]=component(base+weights->offset_B,weights->component_type,c,true);if(values[c]<0) fail(QStringLiteral("GLB: negative skin weight."));sum+=values[c]; }if(sum<1e-12) fail(QStringLiteral("GLB: zero skin weights."));for(double v:values) Builder::f32(data[6],v/sum); }
			if(tangents) { if(!normals) fail(QStringLiteral("GLB: tangents require authored normals."));const auto t=normal(mesh,vi,*tangents,true);const V tangent{{t[0],t[1],t[2]}};V bitangent=cross(n,tangent);for(double& v:bitangent) v*=t[3];const double det=texture_matrix.determinant();if(std::abs(det)<1e-12) fail(QStringLiteral("GLB: singular UV transform cannot preserve tangents."));const Matrix2f inv=texture_matrix.inverse();V nt,nb;for(int c=0;c<3;++c) { nt[c]=tangent[c]*inv.e[0]+bitangent[c]*inv.e[2];nb[c]=tangent[c]*inv.e[1]+bitangent[c]*inv.e[3]; }nt=unit(nt);for(double v:nt) Builder::f32(data[7],v);Builder::f32(data[7],dot(cross(n,nt),nb)<0?-1:1); }
		}
		QJsonObject attributes{{"POSITION",b.accessor(data[0],batch.num_indices,"VEC3",3,5126,34962,true)},{"NORMAL",b.accessor(data[1],batch.num_indices,"VEC3",3)}};
		const char* names[]={"TEXCOORD_0","TEXCOORD_1","COLOR_0","JOINTS_0","WEIGHTS_0","TANGENT"};const char* types[]={"VEC2","VEC2","VEC3","VEC4","VEC4","VEC4"};const int components[]={2,2,3,4,4,4};
		for(int i=0;i<6;++i) if(!data[i+2].isEmpty()) attributes[names[i]]=b.accessor(data[i+2],batch.num_indices,types[i],components[i],i==3?5123:5126);
		primitives.append(QJsonObject{{"attributes",attributes},{"material",(int)batch.material_index},{"mode",4}});
	}
	if(std::find(covered.begin(),covered.end(),false)!=covered.end()) fail(QStringLiteral("GLB: material batches do not cover the mesh."));return primitives;
}

static void skeleton(Builder& b,const BatchedMesh& mesh,QJsonObject& root,QJsonArray& nodes,QJsonArray& root_children) {
	const auto& anim=mesh.animation_data;if(anim.nodes.empty()) { if(mesh.findAttribute(BatchedMesh::VertAttribute_Joints)) fail(QStringLiteral("GLB: skin attributes have no skeleton."));return; }
	if(anim.nodes.size()>1024||anim.animations.size()>128||anim.retarget_adjustments_set||anim.vrm_data.nonNull()) fail(QStringLiteral("Retargeted/VRM extension rigs or rigs above 1024 bones/128 clips need a dedicated exporter."));anim.checkDataIsValid();
	for(size_t i=0;i<anim.nodes.size();++i) { const auto& bone=anim.nodes[i];if(bone.parent_index>= (int)anim.nodes.size()||bone.parent_index==(int)i) fail(QStringLiteral("GLB: invalid skeleton parent."));QJsonArray children;for(size_t j=0;j<anim.nodes.size();++j) if(anim.nodes[j].parent_index==(int)i) children.append((int)j+2);
		QJsonObject node{{"name",QString::fromUtf8(bone.name.c_str())},{"translation",array({{bone.trans[0],bone.trans[1],bone.trans[2]}})},{"rotation",QJsonArray{bone.rot.v[0],bone.rot.v[1],bone.rot.v[2],bone.rot.v[3]}},{"scale",array({{bone.scale[0],bone.scale[1],bone.scale[2]}})}};if(!children.isEmpty()) node["children"]=children;nodes.append(node);if(bone.parent_index<0) root_children.append((int)i+2);
	}
	if(!anim.joint_nodes.empty()) { if(!mesh.findAttribute(BatchedMesh::VertAttribute_Joints)) fail(QStringLiteral("GLB: skeleton has no vertex binding."));QByteArray inverse_bind;QJsonArray joint_nodes;for(int id:anim.joint_nodes) { if(id<0||id>=(int)anim.nodes.size()) fail(QStringLiteral("GLB: invalid joint node."));joint_nodes.append(id+2);const Matrix4f& matrix=anim.nodes[id].inverse_bind_matrix;for(int c=0;c<4;++c) for(int r=0;r<4;++r) Builder::f32(inverse_bind,matrix.elem(r,c)); }root["skins"]=QJsonArray{QJsonObject{{"joints",joint_nodes},{"inverseBindMatrices",b.accessor(inverse_bind,(int)anim.joint_nodes.size(),"MAT4",16,5126,0)}}};QJsonObject mesh_node=nodes[1].toObject();mesh_node["skin"]=0;nodes[1]=mesh_node; }
	QJsonArray animations;
	for(const auto& clip:anim.animations) {
		if(clip->raw_per_anim_node_data.size()!=anim.nodes.size()||!clip->m_keyframe_times.empty()||!clip->m_output_data.empty()) fail(QStringLiteral("GLB: unsupported shared or inconsistent animation clip."));QJsonArray samplers,channels;
		for(size_t node=0;node<anim.nodes.size();++node) { const auto& d=clip->raw_per_anim_node_data[node];const int inputs[]={d.translation_input_accessor,d.rotation_input_accessor,d.scale_input_accessor};const int outputs[]={d.translation_output_accessor,d.rotation_output_accessor,d.scale_output_accessor};const char* names[]={"translation","rotation","scale"};
			for(int channel=0;channel<3;++channel) { if(inputs[channel]<0&&outputs[channel]<0) continue;if(inputs[channel]<0||outputs[channel]<0||inputs[channel]>=(int)anim.keyframe_times.size()||outputs[channel]>=(int)anim.output_data.size()) fail(QStringLiteral("GLB: invalid animation accessor."));const auto& times=anim.keyframe_times[inputs[channel]].times;const auto& values=anim.output_data[outputs[channel]];if(times.empty()||times.size()!=values.size()||times.size()>100000) fail(QStringLiteral("GLB: invalid animation keyframe count."));QByteArray time_bytes,value_bytes;const int components=channel==1?4:3;for(size_t k=0;k<times.size();++k) { if(times[k]<0||(k&&times[k]<=times[k-1])) fail(QStringLiteral("GLB: animation times must increase."));Builder::f32(time_bytes,times[k]);for(int c=0;c<components;++c) Builder::f32(value_bytes,values[k][c]); }const int sampler=samplers.size();samplers.append(QJsonObject{{"input",b.accessor(time_bytes,(int)times.size(),"SCALAR",1,5126,0,true)},{"output",b.accessor(value_bytes,(int)times.size(),components==4?"VEC4":"VEC3",components,5126,0)},{"interpolation","LINEAR"}});channels.append(QJsonObject{{"sampler",sampler},{"target",QJsonObject{{"node",(int)node+2},{"path",names[channel]}}}}); }
		}
		if(channels.isEmpty()) fail(QStringLiteral("GLB: animation clip has no exportable channels."));animations.append(QJsonObject{{"name",QString::fromUtf8(clip->name.c_str())},{"samplers",samplers},{"channels",channels}});
	}
	if(!animations.isEmpty()) root["animations"]=animations;
}

static void write(const WorldObject& ob,const std::map<std::string,std::string>& paths,const QString& output) {
	Reference<glare::LimitedAllocator> allocator=new glare::LimitedAllocator(256*1024*1024);Reference<BatchedMesh> mesh;
	if(ob.object_type==WorldObject::ObjectType_VoxelGroup) { VoxelGroup group;const auto compressed=ob.getCompressedVoxels();if(compressed.isNull()||compressed->empty()) fail(QStringLiteral("Voxel object has no voxel data."));WorldObject::decompressVoxelGroup(compressed->data(),compressed->size(),allocator.ptr(),group);if(group.voxels.size()>250000) fail(QStringLiteral("GLB voxel export is limited to 250000 voxels."));js::Vector<bool,16> transparent;for(const auto& m:ob.materials) transparent.push_back(m->opacity.val<1);const auto native=VoxelMeshBuilding::makeIndigoMeshWithShadingNormalsForVoxelGroup(group,1,transparent,allocator.ptr());mesh=BatchedMesh::buildFromIndigoMesh(*native); }
	else { const auto path=paths.find(toStdString(ob.model_url));if(path==paths.end()) fail(QStringLiteral("Original model resource is unavailable."));if(FileUtils::getFileSize(path->second)>64*1024*1024) fail(QStringLiteral("Source model exceeds 64 MiB."));mesh=BatchedMesh::readFromFile(path->second,allocator.ptr()); }
	mesh->checkValidAndSanitiseMesh();Builder b;addMaterials(b,ob,paths);QJsonObject root{{"asset",QJsonObject{{"version","2.0"},{"generator","Metasiberia object export"}}},{"scene",0},{"scenes",QJsonArray{QJsonObject{{"nodes",QJsonArray{0}}}}}};
	root["meshes"]=QJsonArray{QJsonObject{{"primitives",geometry(b,*mesh,ob)}}};
	const Matrix4f transform=Matrix4f::rotationAroundXAxis(-1.5707963267948966f)*ob.obToWorldMatrix();QJsonArray root_children{1};QJsonArray nodes{QJsonObject{{"name",QStringLiteral("Object ")+QString::number(ob.uid.value())},{"matrix",matrixJSON(transform)}},QJsonObject{{"mesh",0}}};skeleton(b,*mesh,root,nodes,root_children);QJsonObject scene_root=nodes[0].toObject();scene_root["children"]=root_children;
	scene_root["extras"]=QJsonObject{{"metasiberia_uid",QString::number(ob.uid.value())},{"metasiberia_script",QString::fromUtf8(ob.script.c_str())},{"metasiberia_content",QString::fromUtf8(ob.content.c_str())}};nodes[0]=scene_root;root["nodes"]=nodes;
	while(b.bin.size()%4) b.bin.append('\0');root["buffers"]=QJsonArray{QJsonObject{{"byteLength",b.bin.size()}}};root["bufferViews"]=b.views;root["accessors"]=b.accessors;root["materials"]=b.materials;
	if(!b.images.isEmpty()) { root["images"]=b.images;root["textures"]=b.textures;root["samplers"]=QJsonArray{QJsonObject{{"wrapS",10497},{"wrapT",10497},{"magFilter",9729},{"minFilter",9987}}}; }
	if(!b.extensions.empty()) { QJsonArray extensions;for(const QString& extension:b.extensions) extensions.append(extension);root["extensionsUsed"]=extensions; }
	QByteArray json=QJsonDocument(root).toJson(QJsonDocument::Compact);while(json.size()%4) json.append(' ');if(json.size()>32*1024*1024) fail(QStringLiteral("GLB JSON exceeds 32 MiB."));
	QSaveFile file(output);file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly)) fail(QStringLiteral("Cannot create GLB: ")+file.errorString());
	auto uint32write=[&](quint32 value) { value=qToLittleEndian(value);if(file.write((const char*)&value,4)!=4) fail(file.errorString()); };
	uint32write(0x46546c67);uint32write(2);uint32write((quint32)(12+8+json.size()+8+b.bin.size()));uint32write(json.size());uint32write(0x4e4f534a);if(file.write(json)!=json.size()) fail(file.errorString());uint32write(b.bin.size());uint32write(0x004e4942);if(file.write(b.bin)!=b.bin.size()) fail(file.errorString());if(!file.commit()) fail(QStringLiteral("Cannot atomically save GLB: ")+file.errorString());
}
} // namespace

void start(GUIClient& client,const WorldObject& object,const QString& output_path,QObject* callback_context,std::function<void(const QString&)> completion) {
	if(!callback_context||!client.world_state||client.resource_manager.isNull()) fail(QStringLiteral("No active world for GLB export."));if(output_path.isEmpty()) fail(QStringLiteral("Choose a GLB destination."));
	WorldObjectRef snapshot=new WorldObject();
	{ WorldStateLock lock(client.world_state->mutex);auto found=client.world_state->objects.find(object.uid);if(found==client.world_state->objects.end()||found.getValue().ptr()!=&object||object.state==WorldObject::State_Dead) fail(QStringLiteral("The selected object no longer exists."));if(!ObjectCopyPermissions::canCopyObject(object,client.logged_in_user_id,client.connected_world_details.owner_id,client.world_state->parcels)) fail(QStringLiteral("You may export only your own objects in your own parcels or personal world."));snapshot->copyNetworkStateFrom(object);snapshot->uid=object.uid;snapshot->transformChanged();snapshot->materials.clear();for(const auto& m:object.materials) { if(m.isNull()) fail(QStringLiteral("Object has an empty material slot."));snapshot->materials.push_back(m->clone()); } }
	if(snapshot->object_type!=WorldObject::ObjectType_Generic&&snapshot->object_type!=WorldObject::ObjectType_VoxelGroup) fail(QStringLiteral("GLB export supports native model/primitive and voxel objects. This object type has no exportable static mesh."));
	if(snapshot->object_type==WorldObject::ObjectType_Generic&&!hasExtension(toStdString(snapshot->model_url),"bmesh")) fail(QStringLiteral("GLB export requires the original native .bmesh model."));
	// World-baked lighting is scene-specific; export the object's PBR materials.
	std::set<std::string> urls;if(snapshot->object_type==WorldObject::ObjectType_Generic) urls.insert(toStdString(snapshot->model_url));for(const auto& m:snapshot->materials) for(const auto& url:{m->colour_texture_url,m->normal_map_url,m->roughness.texture_url,m->emission_texture_url}) if(!url.empty()) urls.insert(toStdString(url));
	std::map<std::string,std::string> paths;QStringList missing;
	for(const auto& url:urls) { const URLString resource_url=toURLString(url);if(!client.resource_manager->isFileForURLPresent(resource_url)) { DownloadingResourceInfo info;info.used_by_other=true;info.build_physics_ob=false;info.pos=snapshot->pos;info.size_factor=1;client.startDownloadingResource(resource_url,snapshot->getCentroidWS(),snapshot->getAABBWSLongestLength(),info);missing.append(QString::fromUtf8(url.c_str())); }else paths[url]=client.resource_manager->pathForURL(resource_url); }
	if(!missing.isEmpty()) fail(QStringLiteral("Original resources are downloading. Retry export when they finish:\n")+missing.mid(0,5).join('\n'));
	const auto error=std::make_shared<QString>();QThread* worker=QThread::create([snapshot,paths,output_path,error]() {
		try { write(*snapshot,paths,output_path); }catch(const glare::Exception& e) { *error=QString::fromUtf8(e.what().c_str()); }catch(const std::exception& e) { *error=QString::fromUtf8(e.what()); }catch(...) { *error=QStringLiteral("Unexpected GLB export error."); }
	});
	// Qt disconnects this queued completion automatically if the UI owner is destroyed.
	// The worker never reads a QObject pointer or a live GUIClient/WorldObject.
	QObject::connect(worker,&QThread::finished,callback_context,[completion,error]() { if(completion) completion(*error); },Qt::QueuedConnection);
	QObject::connect(worker,&QThread::finished,worker,&QObject::deleteLater);worker->start();
}
} // namespace ObjectGLBExport
#endif
