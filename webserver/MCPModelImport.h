// Included inside MCPHandlers::Modeling, after the native snapshot/publication helpers.
// Conversion reads only the resource already assigned to an editable world object.
#pragma once

static uint32 modelLE32(const std::string& data,size_t offset)
{
	if(offset+4>data.size()) throw glare::Exception("Truncated GLB header.");
	return (uint32)(uint8)data[offset] | ((uint32)(uint8)data[offset+1]<<8) | ((uint32)(uint8)data[offset+2]<<16) | ((uint32)(uint8)data[offset+3]<<24);
}

static void checkEmbeddedGLTF(const std::string& data,bool binary)
{
	if(data.size()>16*1024*1024) throw glare::Exception("Model conversion source limit is 16 MiB.");
	size_t start=0,length=data.size(),bin_length=0;
	if(binary) {
		if(data.size()<20 || modelLE32(data,0)!=0x46546c67 || modelLE32(data,4)!=2 || modelLE32(data,8)!=data.size() || modelLE32(data,16)!=0x4e4f534a)
			throw glare::Exception("Expected a valid GLB 2.0 resource.");
		start=20;length=modelLE32(data,12);if(length>data.size()-start) throw glare::Exception("Truncated GLB JSON chunk.");
		const size_t bin_start=(start+length+3)&~size_t(3);
		if(bin_start+8>data.size() || modelLE32(data,bin_start+4)!=0x004e4942) throw glare::Exception("Expected an embedded GLB BIN chunk.");
		bin_length=modelLE32(data,bin_start);
		if(bin_length>data.size()-bin_start-8) throw glare::Exception("Truncated GLB BIN chunk.");
	}
	// Bound JSONParser allocations as well as decoder allocations. Base64 strings
	// do not count towards this structural budget.
	bool in_string=false,escape=false; size_t structural=0,depth=0;
	for(size_t i=start;i<start+length;++i) {
		const char c=data[i];
		if(in_string) { if(escape) escape=false; else if(c=='\\') escape=true; else if(c=='"') in_string=false; continue; }
		if(c=='"') in_string=true;
		else if(c=='{' || c=='[') { if(++depth>64) throw glare::Exception("glTF JSON nesting exceeds 64."); ++structural; }
		else if(c=='}' || c==']') { if(depth==0) throw glare::Exception("Malformed glTF JSON."); --depth; }
		else if(c==',' || c==':') ++structural;
		if(structural>200000) throw glare::Exception("glTF metadata exceeds the conversion budget.");
	}
	JSONParser json;json.parseBuffer(data.data()+start,length);
	const JSONNode& root=json.nodes[0]; checkNodeType(root,JSONNode::Type_Object);
	for(const JSONNode& node:json.nodes) {
		if(node.type==JSONNode::Type_Number && !std::isfinite(node.getDoubleValue())) throw glare::Exception("glTF numbers must be finite.");
		if(node.type!=JSONNode::Type_Object || !node.hasChild("uri")) continue;
		const std::string uri=node.getChildStringValue(json,"uri");
		if(!hasPrefix(uri,"data:")) throw glare::Exception("Conversion requires embedded GLB/glTF buffers and images. Package external files into GLB in the model importer first.");
	}
	const auto bounded_uint=[&](const JSONNode& node,const char* key,size_t fallback,size_t max) {
		const double value=node.getChildDoubleValueWithDefaultVal(json,key,(double)fallback);
		if(!std::isfinite(value) || value<0 || value>(double)max || value!=std::floor(value)) throw glare::Exception(std::string("glTF ")+key+" is outside the conversion budget.");
		return (size_t)value;
	};
	const auto array=[&](const char* key,size_t max) {
		std::vector<uint32> indices;
		if(root.hasChild(key)) { const auto& a=root.getChildArray(json,key); if(a.child_indices.size()>max) throw glare::Exception(std::string("Too many glTF ")+key+"."); indices=a.child_indices; }
		return indices;
	};
	const auto buffers=array("buffers",64), views=array("bufferViews",4096), accessors=array("accessors",4096), nodes=array("nodes",4096), meshes=array("meshes",4096);
	array("materials",255); array("images",255); array("textures",255);
	const auto skins=array("skins",1), animations=array("animations",64), scenes=array("scenes",64);
	std::vector<size_t> buffer_sizes,view_lengths,view_strides,accessor_counts; size_t total_buffers=0,total_accessor_bytes=0;
	for(uint32 id:buffers) {
		const auto& b=json.nodes[id]; const size_t bytes=bounded_uint(b,"byteLength",0,16*1024*1024);
		if(!bytes || (total_buffers+=bytes)>32*1024*1024) throw glare::Exception("glTF buffers exceed the conversion budget.");
		if(!b.hasChild("uri")) { if(!binary || buffer_sizes.size()!=0 || bytes>bin_length) throw glare::Exception("Invalid embedded GLB buffer length."); }
		else {
			const std::string& uri=b.getChildStringValue(json,"uri"); const size_t comma=uri.find(',');
			if(comma==std::string::npos || uri.substr(0,comma).find(";base64")==std::string::npos || bytes>((uri.size()-comma-1)/4)*3)
				throw glare::Exception("Embedded glTF buffer is shorter than byteLength.");
		}
		buffer_sizes.push_back(bytes);
	}
	for(uint32 id:views) {
		const auto& view=json.nodes[id]; const size_t buffer=bounded_uint(view,"buffer",buffers.size(),buffers.size());
		const size_t offset=bounded_uint(view,"byteOffset",0,16*1024*1024), bytes=bounded_uint(view,"byteLength",0,16*1024*1024), stride=bounded_uint(view,"byteStride",0,252);
		if(buffer>=buffers.size() || offset>buffer_sizes[buffer] || bytes>buffer_sizes[buffer]-offset || (stride && (stride<4 || stride%4))) throw glare::Exception("Invalid glTF buffer view bounds or stride.");
		view_lengths.push_back(bytes);view_strides.push_back(stride);
	}
	for(uint32 id:accessors) {
		const auto& a=json.nodes[id]; if(a.hasChild("sparse")) throw glare::Exception("Sparse accessors require conversion through the client importer first.");
		const size_t count=bounded_uint(a,"count",0,300000), view=bounded_uint(a,"bufferView",views.size(),views.size()), offset=bounded_uint(a,"byteOffset",0,16*1024*1024);
		const size_t component=bounded_uint(a,"componentType",0,5126); const std::string type=a.getChildStringValue(json,"type");
		const size_t components=type=="SCALAR"?1:type=="VEC2"?2:type=="VEC3"?3:type=="VEC4"?4:type=="MAT2"?4:type=="MAT3"?9:type=="MAT4"?16:0;
		const size_t width=(component==5120 || component==5121)?1:(component==5122 || component==5123)?2:(component==5125 || component==5126)?4:0;
		if(!count || !components || !width || view>=views.size()) throw glare::Exception("Invalid glTF accessor.");
		const size_t element=components*width, stride=view_strides[view]?view_strides[view]:element;
		if(stride<element || offset>view_lengths[view] || element>view_lengths[view]-offset || (count-1)>(view_lengths[view]-offset-element)/stride)
			throw glare::Exception("glTF accessor extends outside its buffer view.");
		total_accessor_bytes+=count*std::max(size_t(16),element);
		if(total_accessor_bytes>64*1024*1024) throw glare::Exception("glTF accessor allocations exceed 64 MiB.");
		accessor_counts.push_back(count);
	}
	std::vector<std::pair<size_t,size_t>> mesh_counts;
	for(uint32 id:meshes) {
		const auto& primitives=json.nodes[id].getChildArray(json,"primitives");
		if(primitives.child_indices.size()>4096) throw glare::Exception("Too many glTF primitives.");
		size_t vertices=0,indices=0;
		for(uint32 pid:primitives.child_indices) {
			const auto& primitive=json.nodes[pid]; const auto& attrs=primitive.getChildObject(json,"attributes");
			if(primitive.hasChild("targets")) throw glare::Exception("Morph targets are not preserved by the native importer; convert a deliberately baked copy first.");
			if(bounded_uint(primitive,"mode",4,6)!=4) throw glare::Exception("Model conversion requires triangle primitives.");
			for(const auto& attr:attrs.name_val_pairs)
				if(attr.name!="POSITION" && attr.name!="NORMAL" && attr.name!="TANGENT" && attr.name!="TEXCOORD_0" && attr.name!="COLOR_0" && attr.name!="JOINTS_0" && attr.name!="WEIGHTS_0")
					throw glare::Exception("The native importer cannot preserve attribute '"+attr.name+"'; convert it explicitly in the client first.");
			const size_t pos=bounded_uint(attrs,"POSITION",accessors.size(),accessors.size());
			if(pos>=accessors.size()) throw glare::Exception("glTF primitive has no valid POSITION accessor.");
			vertices+=accessor_counts[pos];
			const size_t index=bounded_uint(primitive,"indices",accessors.size(),accessors.size());
			if(primitive.hasChild("indices") && index>=accessors.size()) throw glare::Exception("Invalid glTF index accessor.");
			indices+=index==accessors.size()?accessor_counts[pos]:accessor_counts[index];
			if(vertices>300000 || indices>300000) throw glare::Exception("glTF mesh exceeds 100000 triangles or 300000 vertices.");
		}
		mesh_counts.emplace_back(vertices,indices);
	}
	std::vector<int> parents(nodes.size(),-1); size_t total_vertices=0,total_indices=0;
	for(size_t i=0;i<nodes.size();++i) {
		const auto& node=json.nodes[nodes[i]];
		if(node.hasChild("mesh")) { const size_t mesh=bounded_uint(node,"mesh",meshes.size(),meshes.size()); if(mesh>=meshes.size()) throw glare::Exception("Invalid glTF mesh index."); total_vertices+=mesh_counts[mesh].first;total_indices+=mesh_counts[mesh].second; }
		if(total_vertices>300000 || total_indices>300000 || total_vertices*80+total_indices*4>64*1024*1024) throw glare::Exception("Instanced glTF geometry exceeds the conversion budget.");
		if(node.hasChild("children")) for(uint32 child:node.getChildArray(json,"children").child_indices) {
			const double raw=json.nodes[child].getDoubleValue();
			if(!std::isfinite(raw) || raw<0 || raw>=nodes.size() || raw!=std::floor(raw) || parents[(size_t)raw]>=0) throw glare::Exception("glTF hierarchy must be a tree with unique children.");
			parents[(size_t)raw]=(int)i;
		}
	}
	for(size_t i=0;i<nodes.size();++i) { size_t depth=0; for(int parent=(int)i;parent>=0;parent=parents[parent]) if(++depth>64) throw glare::Exception("Cyclic or excessively deep glTF node hierarchy."); }
	for(uint32 id:scenes) { std::set<size_t> roots; for(uint32 index:json.nodes[id].getChildArray(json,"nodes").child_indices) { const double raw=json.nodes[index].getDoubleValue(); if(!std::isfinite(raw) || raw<0 || raw>=nodes.size() || raw!=std::floor(raw) || parents[(size_t)raw]>=0 || !roots.insert((size_t)raw).second) throw glare::Exception("Invalid or repeated glTF scene root."); } }
	for(uint32 id:skins) if(json.nodes[id].getChildArray(json,"joints").child_indices.size()>256) throw glare::Exception("Imported skin exceeds 256 joints.");
	for(uint32 id:animations) {
		const auto& animation=json.nodes[id];
		if(animation.getChildArray(json,"channels").child_indices.size()>768 || animation.getChildArray(json,"samplers").child_indices.size()>768) throw glare::Exception("Imported clip exceeds 768 tracks.");
		for(uint32 sid:animation.getChildArray(json,"samplers").child_indices)
			if(json.nodes[sid].getChildStringValueWithDefaultVal(json,"interpolation","LINEAR")!="LINEAR") throw glare::Exception("Only linear animation channels are preserved by this importer; bake other interpolation before conversion.");
	}
}

static void checkSimpleModelBudget(const std::string& data,bool obj)
{
	if(data.size()>16*1024*1024) throw glare::Exception("Model conversion source limit is 16 MiB.");
	if(!obj && !hasPrefix(data,"solid")) {
		if(data.size()<84 || modelLE32(data,80)>100000 || modelLE32(data,80)>(data.size()-84)/50) throw glare::Exception("Invalid or oversized binary STL triangle count.");
		return;
	}
	size_t vertices=0,triangles=0,attributes=0;
	for(size_t start=0;start<data.size();) {
		size_t end=data.find('\n',start);if(end==std::string::npos) end=data.size();
		size_t cursor=start;while(cursor<end && (data[cursor]==' ' || data[cursor]=='\t' || data[cursor]=='\r')) ++cursor;
		const size_t token_start=cursor;while(cursor<end && data[cursor]!=' ' && data[cursor]!='\t' && data[cursor]!='\r') ++cursor;
		const std::string token=data.substr(token_start,cursor-token_start);
		if(obj && (token=="v" || token=="vn" || token=="vt")) { if(++attributes>900000) throw glare::Exception("OBJ vertex attributes exceed the conversion budget."); if(token=="v" && ++vertices>300000) throw glare::Exception("OBJ vertices exceed the conversion budget."); }
		if(obj && token=="f") {
			size_t corners=0;
			while(cursor<end) { while(cursor<end && (data[cursor]==' ' || data[cursor]=='\t' || data[cursor]=='\r')) ++cursor; if(cursor==end || data[cursor]=='#') break; ++corners; while(cursor<end && data[cursor]!=' ' && data[cursor]!='\t' && data[cursor]!='\r') ++cursor; }
			if(corners<3 || corners>256) throw glare::Exception("OBJ faces must have 3..256 corners.");triangles+=corners-2;
		} else if(!obj && token=="facet") ++triangles;
		if(triangles>100000) throw glare::Exception("Conversion exceeds 100000 triangles.");
		start=end==data.size()?end:end+1;
	}
}

// Supply the attributes required by the native modeling tools without changing
// existing UVs, vertex numbers, material batches or skin/animation data.
static void completeImportedAttributes(BatchedMesh& mesh)
{
	mesh.checkValidAndSanitiseMesh();
	if(mesh.numVerts()==0 || mesh.numIndices()==0) throw glare::Exception("Imported mesh is empty.");
	for(size_t i=0;i<mesh.numVerts();++i) { const Vec4f pos=mesh.getVertexPosition((uint32)i);for(int k=0;k<3;++k) if(!std::isfinite(pos[k]) || std::abs(pos[k])>1e7f) throw glare::Exception("Imported vertex position exceeds supported range."); }
	const bool normals=!mesh.findAttribute(BatchedMesh::VertAttribute_Normal),uv=!mesh.findAttribute(BatchedMesh::VertAttribute_UV_0);
	if(!normals && !uv) return;
	if(mesh.numVerts()>300000 || mesh.numIndices()>300000) throw glare::Exception("Imported geometry exceeds the conversion budget.");
	const size_t old_stride=mesh.vertexSize(),normal_offset=old_stride,uv_offset=old_stride+(normals?4:0),stride=uv_offset+(uv?8:0);
	glare::AllocatorVector<uint8,16> vertices;vertices.resize(mesh.numVerts()*stride,0);
	std::vector<Vec3d> sums(normals?mesh.numVerts():0,Vec3d(0,0,0));
	if(normals) for(size_t i=0;i<mesh.numIndices();i+=3) {
		const uint32 a=mesh.getIndexAsUInt32(i),b=mesh.getIndexAsUInt32(i+1),c=mesh.getIndexAsUInt32(i+2);
		const Vec4f p=mesh.getVertexPosition(a),q=mesh.getVertexPosition(b),r=mesh.getVertexPosition(c),cross=crossProduct(q-p,r-p);
		const Vec3d weighted(cross[0],cross[1],cross[2]);sums[a]+=weighted;sums[b]+=weighted;sums[c]+=weighted;
	}
	const Vec4f extent=mesh.aabb_os.max_-mesh.aabb_os.min_;int axis0=0,axis1=1;
	if(extent[2]>extent[axis0]) axis0=2;
	if(extent[1]>extent[axis0]) axis0=1;
	axis1=axis0==0?1:0;for(int k=0;k<3;++k) if(k!=axis0 && extent[k]>extent[axis1]) axis1=k;
	for(size_t i=0;i<mesh.numVerts();++i) {
		std::memcpy(vertices.data()+i*stride,mesh.vertex_data.data()+i*old_stride,old_stride);
		if(normals) { const Vec3d s=sums[i];const double len=std::sqrt(s.x*s.x+s.y*s.y+s.z*s.z);const Vec4f normal=len>1e-20?Vec4f((float)(s.x/len),(float)(s.y/len),(float)(s.z/len),0):Vec4f(0,0,1,0);const uint32 packed=batchedMeshPackNormal(normal);std::memcpy(vertices.data()+i*stride+normal_offset,&packed,4); }
		if(uv) { const Vec4f p=mesh.getVertexPosition((uint32)i);const float pair[2]={(p[axis0]-mesh.aabb_os.min_[axis0])/std::max(extent[axis0],1e-10f),(p[axis1]-mesh.aabb_os.min_[axis1])/std::max(extent[axis1],1e-10f)};std::memcpy(vertices.data()+i*stride+uv_offset,pair,8); }
	}
	if(normals) mesh.vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Normal,BatchedMesh::ComponentType_PackedNormal,normal_offset));
	if(uv) { mesh.vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_UV_0,BatchedMesh::ComponentType_Float,uv_offset));mesh.uv0_scale=1; }
	mesh.vertex_data.swapWith(vertices);mesh.checkValidAndSanitiseMesh();
}

static Reference<BatchedMesh> simplifyNativeModel(const BatchedMesh& source,size_t target,float error)
{
	const size_t count=source.numIndices()/3;
	if(count<5 || count>100000 || source.numVerts()>300000 || source.getTotalMemUsage()>64*1024*1024) throw glare::Exception("Simplification exceeds the geometry budget.");
	if(target<4 || target>=count || !std::isfinite(error) || error<=0 || error>.1f) throw glare::Exception("Invalid simplification target or error.");
	if(source.getAttribute(BatchedMesh::VertAttribute_Position).component_type!=BatchedMesh::ComponentType_Float) throw glare::Exception("Simplification requires native float vertex positions.");
	Reference<BatchedMesh> result=MeshSimplification::buildSimplifiedMesh(source,(float)count/(float)target,error,false);
	result->uv0_scale=source.uv0_scale;result->uv1_scale=source.uv1_scale;
	result->checkValidAndSanitiseMesh();
	if(result->numIndices()>=source.numIndices()) throw glare::Exception("No safe reduction within max_error; no object was changed. Increase the error allowance deliberately or select a smaller component.");
	if(result->numIndices()==0) throw glare::Exception("Simplification produced an empty mesh; no object was changed.");
	return result;
}

#ifndef MCP_MODEL_IMPORT_HELPERS_ONLY

static std::string convertMesh(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	const double raw_uid=number(p,n,"replace_uid",0,0,9007199254740991.0);
	if(!n.hasChild("replace_uid") || raw_uid!=std::floor(raw_uid)) throw glare::Exception("replace_uid must be an integer.");
	WorldObjectRef ob=new WorldObject();
	{
		WorldStateLock lock(all.mutex);WorldObjectRef target=findObject(w,lock,UID((uint64)raw_uid));
		if(!editable(*target,w,user)) throw glare::Exception("Permission denied for mesh conversion.");
		if(target->object_type!=WorldObject::ObjectType_Generic) throw glare::Exception("Conversion requires a model object.");
		ob->copyNetworkStateFrom(*target);
	}
	const std::string url=toStdString(ob->model_url);
	if(hasExtension(url,"bmesh")) return describe(*ob);
	const bool obj=hasExtension(url,"obj"),stl=hasExtension(url,"stl"),gltf=hasExtension(url,"gltf"),glb=hasExtension(url,"glb")||hasExtension(url,"vrm");
	if(!obj&&!stl&&!gltf&&!glb) throw glare::Exception("Supported conversion formats: OBJ, STL, embedded glTF, GLB and VRM. Import other formats through the client first.");
	const std::string path=all.resource_manager->pathForURL(ob->model_url);
	if(!FileUtils::fileExists(path)) throw glare::Exception("The model resource must be uploaded to the server before conversion.");
	if(FileUtils::getFileSize(path)>16*1024*1024) throw glare::Exception("Model conversion source limit is 16 MiB.");
	const std::string data=FileUtils::readEntireFile(path);Reference<BatchedMesh> result;
	if(obj||stl) {
		checkSimpleModelBudget(data,obj);
		Indigo::Mesh mesh;
		if(obj) { MLTLibMaterials unused;FormatDecoderObj::loadModelFromBuffer((const uint8*)data.data(),data.size(),"",mesh,1.f,false,unused); }
		else FormatDecoderSTL::loadModelFromBuffer((const uint8*)data.data(),data.size(),mesh,1.f);
		if(mesh.triangles.size()+mesh.quads.size()*2>100000) throw glare::Exception("Conversion exceeds 100000 triangles.");
		result=BatchedMesh::buildFromIndigoMesh(mesh);
	} else {
		checkEmbeddedGLTF(data,glb);GLTFLoadedData unused;
		result=glb ? FormatDecoderGLTF::loadGLBFileFromData(data.data(),data.size(),"",false,unused) : FormatDecoderGLTF::loadGLTFFileFromData(data.data(),data.size(),"",false,unused);
	}
	completeImportedAttributes(*result);
	if(result->numIndices()/3>100000 || result->getTotalMemUsage()>64*1024*1024) throw glare::Exception("Converted model exceeds the geometry budget.");
	if(result->numMaterialsReferenced()>ob->materials.size()) throw glare::Exception("The imported model references material slots missing from the object. Import its materials in the client before conversion.");
	ob->setAABBOS(result->aabb_os);const std::vector<WorldObjectRef> sources{ob};
	return publish(all,w,ob,p,n,user,username,result,&sources);
}

static std::string simplifyMesh(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username)
{
	WorldObjectRef ob=meshSnapshot(all,w,p,n,user,"replace_uid");Reference<BatchedMesh> source=readNativeMesh(all,*ob);
	const size_t count=source->numIndices()/3;
	if(count<5 || count>100000) throw glare::Exception("Simplification requires 5..100000 triangles.");
	const int target=integer(p,n,"target_triangles",std::max(4,std::min(4096,(int)count/2)),4,(int)count-1);
	const float error=(float)number(p,n,"max_error",.001,.000001,.1);
	// Native simplifier retains raw vertex attributes, skin weights and AnimationData.
	Reference<BatchedMesh> result=simplifyNativeModel(*source,(size_t)target,error);
	ob->setAABBOS(result->aabb_os);const std::vector<WorldObjectRef> sources{ob};
	const std::string updated=publish(all,w,ob,p,n,user,username,result,&sources);
	return "{\"object\":"+updated+",\"triangles_before\":"+toString(count)+",\"triangles_after\":"+toString(result->numIndices()/3)+",\"target_reached\":"+(result->numIndices()/3<=(size_t)target?"true":"false")+"}";
}
#endif
