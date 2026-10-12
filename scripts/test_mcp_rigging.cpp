// Native rigging regression checks. Link against the server's glare graphics,
// maths, JSON and file libraries. World publication is a local in-memory stub;
// no server connection, world mutation or production resources are used.
#include <graphics/BatchedMesh.h>
#include <utils/JSONParser.h>
#include <utils/StringUtils.h>
#include <utils/FileUtils.h>
#include <utils/Exception.h>
#include <webserver/Escaping.h>
#include <maths/Matrix4f.h>
#include <array>
#include <set>
#include <cstring>
#include <cmath>
#include <memory>
#include <iostream>
#include <stdexcept>

namespace RigTest {
struct UserID {};
struct ServerWorldState {};
struct WorldObject { struct ID { uint64 value() const { return 7; } } uid; };
using WorldObjectRef=std::shared_ptr<WorldObject>;
struct ServerAllWorldsState { Reference<BatchedMesh> mesh; };
static double number(const JSONParser& p,const JSONNode& n,const std::string& key,double fallback,double lo,double hi)
{
	const double v=n.getChildDoubleValueWithDefaultVal(p,key,fallback);
	if(!std::isfinite(v) || v<lo || v>hi) throw glare::Exception("number out of bounds"); return v;
}
static int integer(const JSONParser& p,const JSONNode& n,const std::string& key,int fallback,int lo,int hi)
{
	const double v=number(p,n,key,fallback,lo,hi); if(v!=std::floor(v)) throw glare::Exception("not integer"); return (int)v;
}
static Vec3d vector(const JSONParser& p,const JSONNode& n,const std::string& key,const Vec3d& fallback,double lo,double hi)
{
	if(!n.hasChild(key)) return fallback; double v[3]; n.getChildArray(p,key).parseDoubleArrayValues(p,3,v);
	for(double x:v) if(!std::isfinite(x) || x<lo || x>hi) throw glare::Exception("vector out of bounds"); return Vec3d(v[0],v[1],v[2]);
}
static std::string vecJSON(const Vec3d& v) { return "["+doubleToString(v.x)+","+doubleToString(v.y)+","+doubleToString(v.z)+"]"; }
static WorldObjectRef meshSnapshot(ServerAllWorldsState&,ServerWorldState&,const JSONParser&,const JSONNode&,const UserID&,const std::string& ="uid") { return std::make_shared<WorldObject>(); }
static Reference<BatchedMesh> readNativeMesh(ServerAllWorldsState& all,const WorldObject&) { return all.mesh; }
static std::string publish(ServerAllWorldsState&,ServerWorldState&,WorldObjectRef,const JSONParser&,const JSONNode&,const UserID&,const std::string&,
	const Reference<BatchedMesh>&,const std::vector<WorldObjectRef>*) { return "{}"; }
#include "../webserver/MCPRigging.h"
}

static void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class F> static void rejects(F action) { bool rejected=false; try { action(); } catch(const glare::Exception&) { rejected=true; } require(rejected,"expected validation rejection"); }

int main()
{
	using namespace RigTest;
	ServerAllWorldsState all; ServerWorldState world; UserID user;
	all.mesh=new BatchedMesh(); auto& mesh=*all.mesh;
	mesh.vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Position,BatchedMesh::ComponentType_Float,0));
	mesh.vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Normal,BatchedMesh::ComponentType_PackedNormal,12));
	mesh.vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_UV_0,BatchedMesh::ComponentType_Float,16));
	mesh.vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Colour,BatchedMesh::ComponentType_Float,24));
	const size_t stride=mesh.vertexSize(); require(stride==36,"unexpected test stride");
	mesh.vertex_data.resize(3*stride,0);
	const float positions[3][3]={{0,0,0},{1,0,0},{0,1,0}};
	for(size_t i=0;i<3;++i)
	{
		std::memcpy(mesh.vertex_data.data()+i*stride,positions[i],12);
		const uint32 packed=batchedMeshPackNormal(Vec4f(0,0,1,0)); std::memcpy(mesh.vertex_data.data()+i*stride+12,&packed,4);
		const float rest[5]={(float)i/2,1-(float)i/2,.2f,.4f,.6f}; std::memcpy(mesh.vertex_data.data()+i*stride+16,rest,20);
	}
	const auto original=mesh.vertex_data;
	js::Vector<uint32,16> indices; indices.push_back(0); indices.push_back(1); indices.push_back(2); mesh.setIndexDataFromIndices(indices,3);
	BatchedMesh::IndicesBatch batch; batch.indices_start=0;batch.num_indices=3;batch.material_index=0;mesh.batches.push_back(batch);mesh.aabb_os=mesh.computeAABB();
	auto command=[&](const std::string& json,int operation) {
		JSONParser parser; parser.parseBuffer(json.data(),json.size());
		if(operation==0) return createSkeleton(all,world,parser,parser.nodes[0],user,"test");
		if(operation==1) return setSkinWeights(all,world,parser,parser.nodes[0],user,"test");
		if(operation==2) return setAnimationClip(all,world,parser,parser.nodes[0],user,"test");
		return getRig(all,world,parser,parser.nodes[0],user);
	};
	command(R"({"replace_uid":7,"bones":[{"name":"root","parent":-1},{"name":"tip","parent":0,"translation":[0,1,0]}]})",0);
	require(mesh.animation_data.nodes.size()==2 && mesh.animation_data.joint_nodes.size()==2,"skeleton absent");
	const auto& tip=mesh.animation_data.nodes[1]; const Matrix4f global=Matrix4f::translationMatrix(0,1,0)*tip.inverse_bind_matrix;
	for(int i=0;i<16;++i) require(std::abs(global.e[i]-(i%5==0?1:0))<1e-6,"incorrect inverse bind");
	for(size_t i=0;i<3;++i)
	{
		require(std::memcmp(original.data()+i*stride,mesh.vertex_data.data()+i*mesh.vertexSize(),stride)==0,"non-skin attributes changed");
		uint16 joints[4];float weights[4];rigReadWeights(mesh,i,joints,weights);float total=0;for(int k=0;k<4;++k) {require(joints[k]<2,"joint outside rig");total+=weights[k];}
		require(std::abs(total-1)<1e-6,"weights not normalised");
	}
	command(R"({"replace_uid":7,"mode":"explicit","assignments":[{"vertex":2,"joints":[1,0],"weights":[0.8,0.2]}]})",1);
	uint16 joints[4];float weights[4];rigReadWeights(mesh,2,joints,weights);require(joints[0]==1 && std::abs(weights[0]-.8f)<1e-6,"explicit weights missing");
	rejects([&] { command(R"({"replace_uid":7,"mode":"explicit","assignments":[{"vertex":2,"joints":[8],"weights":[1]}]})",1); });
	rejects([&] { command(R"({"replace_uid":7,"bones":[{"name":"oops"}]})",0); });
	const std::string animation=R"({"replace_uid":7,"name":"Bend","make_default":true,"tracks":[{"bone":1,"channel":"rotation","times":[0,1,2],"values":[[0,0,0,1],[0,0,0.70710678,0.70710678],[0,0,0,1]]}]})";
	command(animation,2); command(animation,2);
	require(mesh.animation_data.animations.size()==1 && mesh.animation_data.keyframe_times.size()==1,"clip replacement leaked accessors");
	require(std::abs(mesh.animation_data.animations[0]->anim_len-2)<1e-6,"incorrect clip duration");
	rejects([&] { command(R"({"replace_uid":7,"name":"Bad","tracks":[{"bone":1,"channel":"rotation","times":[0,0],"values":[[0,0,0,1],[0,0,0,1]]}]})",2); });
	const std::string inspected=command(R"({"uid":7,"section":"clip","clip_name":"Bend"})",3);
	JSONParser response; response.parseBuffer(inspected.data(),inspected.size()); require(inspected.find("Bend")!=std::string::npos,"inspection missing clip");
	const std::string tmp=FileUtils::writeEntireFileToTempFile("",0);
	try { mesh.writeToFile(tmp); Reference<BatchedMesh> roundtrip=BatchedMesh::readFromFile(tmp,nullptr); roundtrip->checkValidAndSanitiseMesh();
		require(roundtrip->animation_data.nodes.size()==2 && roundtrip->animation_data.animations.size()==1,"rig did not persist");
		rigReadWeights(*roundtrip,2,joints,weights); require(joints[0]==1 && std::abs(weights[0]-.8f)<1e-6,"skin did not persist");
		const auto& anim=roundtrip->animation_data; const int rotation=anim.animations[0]->raw_per_anim_node_data[1].rotation_output_accessor;
		const Matrix4f posed=Matrix4f::translationMatrix(anim.nodes[1].trans)*Quatf(anim.output_data[rotation][1]).toMatrix()*anim.nodes[1].inverse_bind_matrix;
		const Vec4f animated=posed*Vec4f(1,0,0,1);
		require(std::abs(animated[0]-1)<.001f && std::abs(animated[1]-2)<.001f,"persisted clip does not rotate about the bone pivot");
		FileUtils::deleteFile(tmp);
	} catch(...) { FileUtils::deleteFile(tmp); throw; }
	command(R"({"replace_uid":7,"remove":true})",0);
	require(mesh.animation_data.nodes.empty() && mesh.animation_data.animations.empty(),"rig removal failed");
	require(!mesh.findAttribute(BatchedMesh::VertAttribute_Joints) && mesh.vertex_data.size()==original.size(),"skin removal failed");
	require(std::memcmp(mesh.vertex_data.data(),original.data(),original.size())==0,"remove rig changed original vertices");
	std::cout << "Native rig, weights, clips, validation and .bmesh round trip passed.\n";
}
