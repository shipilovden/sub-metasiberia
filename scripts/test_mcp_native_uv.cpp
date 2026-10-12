// Native attribute preservation regression, linked to the Linux server's glare
// graphics/math/file libraries. No server/world connection or publication.
#include <graphics/BatchedMesh.h>
#include <utils/Exception.h>
#include <utils/FileUtils.h>
#include <maths/Matrix4f.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "../webserver/MCPMeshAdvanced.h"
#define MCP_ADVANCED_NATIVE_UV_ONLY
namespace NativeUVTest {
#include "../webserver/MCPAdvancedAdapter.h"
}
#undef MCP_ADVANCED_NATIVE_UV_ONLY
using namespace NativeUVTest;
static void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
template<class F> static void rejects(F action) { bool rejected=false;try { action(); }catch(const glare::Exception&) { rejected=true; }require(rejected,"expected invalid native UV input to fail"); }
static Reference<BatchedMesh> fixture(BatchedMesh::ComponentType pos,BatchedMesh::ComponentType uv)
{
	Reference<BatchedMesh> mesh=new BatchedMesh();
	auto attribute=[&](BatchedMesh::VertAttributeType type,BatchedMesh::ComponentType component) { size_t offset=mesh->vertexSize();if(type==BatchedMesh::VertAttribute_Normal&&component==BatchedMesh::ComponentType_Oct16) offset=6;mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(type,component,offset)); };
	attribute(BatchedMesh::VertAttribute_Position,pos);attribute(BatchedMesh::VertAttribute_Normal,pos==BatchedMesh::ComponentType_UInt16?BatchedMesh::ComponentType_Oct16:BatchedMesh::ComponentType_PackedNormal);
	attribute(BatchedMesh::VertAttribute_UV_0,uv);attribute(BatchedMesh::VertAttribute_UV_1,BatchedMesh::ComponentType_Half);attribute(BatchedMesh::VertAttribute_Colour,BatchedMesh::ComponentType_Float);
	attribute(BatchedMesh::VertAttribute_Joints,BatchedMesh::ComponentType_UInt8);attribute(BatchedMesh::VertAttribute_Weights,BatchedMesh::ComponentType_Float);attribute(BatchedMesh::VertAttribute_Tangent,BatchedMesh::ComponentType_PackedNormal);attribute(BatchedMesh::VertAttribute_MatIndex,BatchedMesh::ComponentType_UInt16);
	const size_t stride=mesh->vertexSize();mesh->vertex_data.resize(4*stride,0);
	const float points[4][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
	for(size_t i=0;i<4;++i) for(const auto& a:mesh->vert_attributes) {
		uint8* dest=mesh->vertex_data.data()+i*stride+a.offset_B;
		if(a.type==BatchedMesh::VertAttribute_Position) for(int c=0;c<3;++c) { if(pos==BatchedMesh::ComponentType_Float) std::memcpy(dest+c*4,&points[i][c],4);else { const uint16 value=points[i][c]==0?0:pos==BatchedMesh::ComponentType_Half?0x3c00:65535;std::memcpy(dest+c*2,&value,2); } }
		else if(a.type==BatchedMesh::VertAttribute_UV_0) for(int c=0;c<2;++c) { const float value=points[i][c];if(uv==BatchedMesh::ComponentType_Float) std::memcpy(dest+c*4,&value,4);else { const uint16 q=value==0?0:uv==BatchedMesh::ComponentType_Half?0x3c00:32768;std::memcpy(dest+c*2,&q,2); } }
		else if(a.type==BatchedMesh::VertAttribute_Weights) { const float weights[4]={1,0,0,0};std::memcpy(dest,weights,sizeof(weights)); }
		else if(a.type==BatchedMesh::VertAttribute_Colour) { const float colour[3]={.1f+(float)i*.1f,.5f,.9f};std::memcpy(dest,colour,sizeof(colour)); }
		else if(a.type==BatchedMesh::VertAttribute_UV_1) { const uint16 halfs[2]={0x3800,0x3c00};std::memcpy(dest,halfs,sizeof(halfs)); }
		else if(a.type==BatchedMesh::VertAttribute_Normal&&pos!=BatchedMesh::ComponentType_UInt16) { const uint32 packed=batchedMeshPackNormal(Vec4f(0,0,1,0));std::memcpy(dest,&packed,4); }
		else if(a.type==BatchedMesh::VertAttribute_MatIndex) { const uint16 index=(uint16)(i%2+3);std::memcpy(dest,&index,2); }
	}
	mesh->uv0_scale=1.0f/32768;mesh->uv1_scale=7;
	mesh->aabb_os=pos==BatchedMesh::ComponentType_UInt16?js::AABBox(Vec4f(10,20,30,1),Vec4f(12,24,30,1)):js::AABBox(Vec4f(0,0,0,1),Vec4f(1,1,0,1));
	js::Vector<uint32,16> indices;for(uint32 i:{0U,1U,2U,0U,2U,3U}) indices.push_back(i);mesh->setIndexDataFromIndices(indices,4);
	mesh->batches.push_back({0,3,3});mesh->batches.push_back({3,3,4});
	AnimationNodeData node;node.name="retained root";node.parent_index=-1;node.trans=Vec4f(0,0,0,1);node.rot=Quatf(Vec4f(0,0,0,1));node.scale=Vec4f(1,1,1,0);node.inverse_bind_matrix=Matrix4f::identity();
	mesh->animation_data.nodes.push_back(node);mesh->animation_data.sorted_nodes.push_back(0);mesh->animation_data.joint_nodes.push_back(0);
	Reference<AnimationDatum> clip=new AnimationDatum();clip->name="retained clip";clip->anim_len=1;clip->raw_per_anim_node_data.resize(1);clip->raw_per_anim_node_data[0].init();mesh->animation_data.animations.push_back(clip);
	mesh->checkValidAndSanitiseMesh();return mesh;
}
static void compare(const BatchedMesh& source,const BatchedMesh& result)
{
	require(result.numIndices()==source.numIndices()&&result.numVerts()==source.numIndices(),"UV split changed corner count");
	require(result.batches==source.batches,"UV edit changed material batches");require(result.aabb_os==source.aabb_os,"UV edit changed quantisation bounds");require(result.uv1_scale==source.uv1_scale,"secondary UV scale changed");
	require(result.animation_data.nodes.size()==1&&result.animation_data.nodes[0].name=="retained root"&&result.animation_data.joint_nodes==source.animation_data.joint_nodes,"UV edit changed rig");
	require(result.animation_data.animations.size()==1&&result.animation_data.animations[0]->name=="retained clip","UV edit changed clips");require(!result.findAttribute(BatchedMesh::VertAttribute_Tangent),"stale tangents retained");
	for(size_t i=0;i<source.numIndices();++i) for(const auto& a:source.vert_attributes) if(a.type!=BatchedMesh::VertAttribute_UV_0&&a.type!=BatchedMesh::VertAttribute_Tangent) {
		const auto& dest=result.getAttribute(a.type);require(dest.component_type==a.component_type,"native format changed");
		require(std::memcmp(source.vertex_data.data()+source.getIndexAsUInt32(i)*source.vertexSize()+a.offset_B,result.vertex_data.data()+result.getIndexAsUInt32(i)*result.vertexSize()+dest.offset_B,BatchedMesh::vertAttributeSize(a))==0,"non-UV vertex data changed");
	}
}
static void run()
{
	for(const auto pos:{BatchedMesh::ComponentType_Float,BatchedMesh::ComponentType_UInt16,BatchedMesh::ComponentType_Half}) for(const auto uv:{BatchedMesh::ComponentType_Float,BatchedMesh::ComponentType_UInt16,BatchedMesh::ComponentType_Half}) {
		const auto source=fixture(pos,uv);auto input=advancedNativeUVInput(*source);require(input.size()==2&&input[0].material==3&&input[1].material==4,"wrong native triangle indexing");require(std::abs(input[0].vertices[1].u-1)<1e-6,"wrong UV decode");
		require(std::abs(input[0].vertices[1].position.x-(pos==BatchedMesh::ComponentType_UInt16?12:1))<1e-6,"wrong position decode");
		MCPMeshOps::UVEdit edit;edit.mode="set_corners";edit.corners.push_back({0,0,.25,.75});auto changed=MCPMeshOps::editUV(input,edit);auto result=advancedNativeUVOutput(*source,changed);result->checkValidAndSanitiseMesh();compare(*source,*result);
		const auto decoded=advancedNativeUVInput(*result);require(decoded[0].vertices[0].u==.25&&decoded[0].vertices[0].v==.75&&decoded[1].vertices[0].u==0,"per-corner seam did not persist");
		if(pos==BatchedMesh::ComponentType_UInt16) require(result->getAttribute(BatchedMesh::VertAttribute_Normal).offset_B==6,"oct16 packing broken");
		const std::string tmp=FileUtils::writeEntireFileToTempFile("",0);try { BatchedMesh::WriteOptions options;for(const auto& a:result->vert_attributes) if(BatchedMesh::vertAttributeSize(a)%4) options.use_meshopt=true;result->writeToFile(tmp,options);auto loaded=BatchedMesh::readFromFile(tmp,nullptr);loaded->checkValidAndSanitiseMesh();compare(*source,*loaded);FileUtils::deleteFile(tmp); }catch(...) { FileUtils::deleteFile(tmp);throw; }
		source->batches[1].indices_start=0;rejects([&]{advancedNativeUVInput(*source);});source->batches[1].indices_start=4;rejects([&]{advancedNativeUVInput(*source);});source->batches.pop_back();rejects([&]{advancedNativeUVInput(*source);});
	}
	auto bad=fixture(BatchedMesh::ComponentType_Float,BatchedMesh::ComponentType_Half);const uint16 infinity=0x7c00;std::memcpy(bad->vertex_data.data()+bad->getAttribute(BatchedMesh::VertAttribute_UV_0).offset_B,&infinity,2);rejects([&]{advancedNativeUVInput(*bad);});
	std::cout<<"Native UV: float/half/quantised input, corner seams, skin/colours/UV1/materials/clips, oct16 packing, bmesh roundtrip and malformed batches passed.\n";
}
int main() { try { run();return 0; }catch(const glare::Exception& e) { std::cerr<<e.what()<<"\n"; }catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; }return 1; }
