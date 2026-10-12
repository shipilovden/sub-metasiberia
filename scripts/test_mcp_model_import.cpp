// Native import regression checks; no server connection or world publication.
#include <graphics/BatchedMesh.h>
#include <graphics/FormatDecoderGLTF.h>
#include <graphics/formatdecoderobj.h>
#include <graphics/FormatDecoderSTL.h>
#include <graphics/MeshSimplification.h>
#include <dll/include/IndigoMesh.h>
#include <utils/JSONParser.h>
#include <utils/StringUtils.h>
#include <utils/Base64.h>
#include <utils/Exception.h>
#include <cmath>
#include <set>
#include <cstring>
#include <stdexcept>
#include <iostream>
#define MCP_MODEL_IMPORT_HELPERS_ONLY
#include "../webserver/MCPModelImport.h"

static void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> static void rejects(F action) { bool rejected=false;try {action();}catch(const glare::Exception&) {rejected=true;}require(rejected,"expected rejection"); }
static void replace(std::string& text,const std::string& from,const std::string& to) {const auto index=text.find(from);require(index!=std::string::npos,"test token absent");text.replace(index,from.size(),to);}
static void appendLE(std::string& data,uint32 value) {for(int i=0;i<4;++i)data.push_back((char)((value>>(8*i))&255));}
static void checkAttributes(BatchedMesh& mesh)
{
	const auto positions=mesh.vertex_data; const size_t old_stride=mesh.vertexSize();const auto batches=mesh.batches;
	completeImportedAttributes(mesh);
	require(mesh.findAttribute(BatchedMesh::VertAttribute_Normal) && mesh.findAttribute(BatchedMesh::VertAttribute_UV_0),"missing modeling attributes");
	require(mesh.batches==batches,"material batches changed");
	for(size_t i=0;i<mesh.numVerts();++i) require(std::memcmp(positions.data()+i*old_stride,mesh.vertex_data.data()+i*mesh.vertexSize(),old_stride)==0,"import changed original vertex attributes");
	const auto& normal=mesh.getAttribute(BatchedMesh::VertAttribute_Normal);uint32 packed;std::memcpy(&packed,mesh.vertex_data.data()+normal.offset_B,4);
	require(std::abs(batchedMeshUnpackNormal(packed)[2]-1)<.005f,"generated geometric normal wrong");
	const auto once=mesh.vertex_data;completeImportedAttributes(mesh);require(mesh.vertex_data.size()==once.size() && std::memcmp(once.data(),mesh.vertex_data.data(),once.size())==0,"completion not idempotent");
}

static void checkSimplification()
{
	BatchedMesh mesh;
	mesh.vert_attributes={{BatchedMesh::VertAttribute_Position,BatchedMesh::ComponentType_Float,0},{BatchedMesh::VertAttribute_Normal,BatchedMesh::ComponentType_PackedNormal,12},
		{BatchedMesh::VertAttribute_UV_0,BatchedMesh::ComponentType_UInt16,16},{BatchedMesh::VertAttribute_UV_1,BatchedMesh::ComponentType_UInt16,20},
		{BatchedMesh::VertAttribute_Colour,BatchedMesh::ComponentType_Float,24},{BatchedMesh::VertAttribute_Joints,BatchedMesh::ComponentType_UInt16,36},
		{BatchedMesh::VertAttribute_Weights,BatchedMesh::ComponentType_Float,44}};
	const size_t stride=mesh.vertexSize();mesh.vertex_data.resize(81*stride,0);mesh.uv0_scale=3.5f;mesh.uv1_scale=4.5f;
	AnimationNodeData root;root.name="root";root.parent_index=-1;root.trans=Vec4f(0);root.rot=Quatf::identity();root.scale=Vec4f(1,1,1,0);root.inverse_bind_matrix=Matrix4f::identity();
	mesh.animation_data.nodes.push_back(root);mesh.animation_data.sorted_nodes.push_back(0);mesh.animation_data.joint_nodes.push_back(0);
	std::set<std::string> rows;
	for(size_t y=0;y<9;++y)for(size_t x=0;x<9;++x){
		uint8* row=mesh.vertex_data.data()+(y*9+x)*stride;const float pos[3]={(float)x,(float)y,0},color[3]={(float)x/8,(float)y/8,.3f},weights[4]={1,0,0,0};
		const uint32 normal=batchedMeshPackNormal(Vec4f(0,0,1,0));const uint16 uv0[2]={(uint16)(x*8000),(uint16)(y*8000)},uv1[2]={(uint16)(y*7000),(uint16)(x*7000)};
		std::memcpy(row,pos,12);std::memcpy(row+12,&normal,4);std::memcpy(row+16,uv0,4);std::memcpy(row+20,uv1,4);std::memcpy(row+24,color,12);std::memcpy(row+44,weights,16);
		rows.insert(std::string((char*)row,stride));
	}
	js::Vector<uint32,16> indices;
	for(uint32 y=0;y<8;++y)for(uint32 x=0;x<8;++x){const uint32 v=y*9+x;for(uint32 i:{v,v+1,v+10,v,v+10,v+9})indices.push_back(i);}
	mesh.setIndexDataFromIndices(indices,81);BatchedMesh::IndicesBatch first{0,192,0},second{192,192,1};mesh.batches={first,second};mesh.aabb_os=mesh.computeAABB();mesh.checkValidAndSanitiseMesh();
	Reference<BatchedMesh> simplified=simplifyNativeModel(mesh,16,.01f);
	require(simplified->numIndices()<mesh.numIndices() && simplified->animation_data.nodes.size()==1,"simplification failed or lost skeleton");
	require(simplified->vert_attributes==mesh.vert_attributes && simplified->uv0_scale==3.5f && simplified->uv1_scale==4.5f,"simplification lost attributes/UV scales");
	for(size_t i=0;i<simplified->numVerts();++i)require(rows.count(std::string((const char*)simplified->vertex_data.data()+i*stride,stride))==1,"simplification modified retained raw vertex/skin data");
	std::set<uint32> materials;for(const auto& batch:simplified->batches)if(batch.num_indices)materials.insert(batch.material_index);require(materials==std::set<uint32>({0,1}),"simplification lost a material region");
}

int main()
{
	const std::string obj="v 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl red\nf 1 2 3\nusemtl blue\nf 1 2 3\n";
	checkSimpleModelBudget(obj,true);Indigo::Mesh obj_mesh;MLTLibMaterials materials;
	FormatDecoderObj::loadModelFromBuffer((const uint8*)obj.data(),obj.size(),"",obj_mesh,1,false,materials);
	Reference<BatchedMesh> imported=BatchedMesh::buildFromIndigoMesh(obj_mesh);checkAttributes(*imported);
	require(imported->numMaterialsReferenced()==2,"OBJ material slots lost");
	const std::string ascii="solid test\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid test\n";
	checkSimpleModelBudget(ascii,false);Indigo::Mesh stl;
	FormatDecoderSTL::loadModelFromBuffer((const uint8*)ascii.data(),ascii.size(),stl,1);
	imported=BatchedMesh::buildFromIndigoMesh(stl);checkAttributes(*imported);
	std::string binary(80,0);appendLE(binary,1);const float face[12]={0,0,1,0,0,0,1,0,0,0,1,0};binary.append((const char*)face,sizeof(face));binary.append(2,0);
	checkSimpleModelBudget(binary,false);Indigo::Mesh binary_stl;FormatDecoderSTL::loadModelFromBuffer((const uint8*)binary.data(),binary.size(),binary_stl,1);
	imported=BatchedMesh::buildFromIndigoMesh(binary_stl);checkAttributes(*imported);
	std::string huge_stl(80,0);appendLE(huge_stl,0xffffffffu);rejects([&]{checkSimpleModelBudget(huge_stl,false);});
	std::string face_overflow="f";for(int i=0;i<257;++i)face_overflow+=" 1";rejects([&]{checkSimpleModelBudget(face_overflow,true);});

	const float verts[9]={0,0,0,1,0,0,0,1,0};std::string encoded;Base64::encode(verts,sizeof(verts),encoded);
	const std::string uri="data:application/octet-stream;base64,"+encoded;
	const std::string json=R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":36,"uri":")"+uri+R"("}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
	checkEmbeddedGLTF(json,false);GLTFLoadedData gltf;imported=FormatDecoderGLTF::loadGLTFFileFromData(json.data(),json.size(),"",false,gltf);checkAttributes(*imported);
	std::string glb_json=json;replace(glb_json,",\"uri\":\""+uri+"\"","");while(glb_json.size()%4)glb_json+=' ';
	std::string glb;appendLE(glb,0x46546c67);appendLE(glb,2);appendLE(glb,(uint32)(28+glb_json.size()+sizeof(verts)));appendLE(glb,(uint32)glb_json.size());appendLE(glb,0x4e4f534a);glb+=glb_json;appendLE(glb,sizeof(verts));appendLE(glb,0x004e4942);glb.append((const char*)verts,sizeof(verts));
	checkEmbeddedGLTF(glb,true);imported=FormatDecoderGLTF::loadGLBFileFromData(glb.data(),glb.size(),"",false,gltf);checkAttributes(*imported);
	for(const auto& mutation:std::vector<std::pair<std::string,std::string>>{
		{"\"count\":3","\"count\":1000000000"},{"\"byteLength\":36","\"byteLength\":4294967295"},
		{uri,"../../private.bin"},{"\"mesh\":0","\"mesh\":0,\"children\":[0]"},{"\"nodes\":[0]","\"nodes\":[0,0]"},
		{"\"POSITION\":0","\"POSITION\":0,\"TEXCOORD_1\":0"},{"\"attributes\":{","\"targets\":[],\"attributes\":{"}}) {
		std::string invalid=json;replace(invalid,mutation.first,mutation.second);rejects([&]{checkEmbeddedGLTF(invalid,false);});
	}
	checkSimplification();
	std::cout<<"Native OBJ/STL/glTF/GLB import, normals/UV completion, allocation guards and attribute-preserving simplification passed.\n";
}
