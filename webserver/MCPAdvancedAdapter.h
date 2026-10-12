// Included in MCPHandlers::Modeling after snapshot, mesh and publication helpers.
#pragma once

// This path must not go through buildIndigoMesh(): that conversion rejects quantised
// native positions/normals/UVs and cannot carry skin, vertex colours or UV1.
static void advancedNativeUVLayout(const BatchedMesh& native)
{
	if(native.vert_attributes.empty()||native.vert_attributes.size()>9) throw glare::Exception("Invalid native attribute count.");
	std::set<BatchedMesh::VertAttributeType> attributes;
	for(const auto& a:native.vert_attributes) if((uint32)a.type>BatchedMesh::MAX_VERT_ATTRIBUTE_TYPE_VALUE||(uint32)a.component_type>BatchedMesh::MAX_COMPONENT_TYPE_VALUE||!attributes.insert(a.type).second) throw glare::Exception("Invalid or duplicate native vertex attribute.");
	const size_t stride=native.vertexSize();
	if(stride==0||native.vertex_data.size()%stride!=0) throw glare::Exception("Invalid native vertex buffer.");
	for(const auto& a:native.vert_attributes) if(a.offset_B>stride||BatchedMesh::vertAttributeSize(a)>stride-a.offset_B) throw glare::Exception("Native attribute exceeds its vertex stride.");
	if(native.index_type!=BatchedMesh::ComponentType_UInt8&&native.index_type!=BatchedMesh::ComponentType_UInt16&&native.index_type!=BatchedMesh::ComponentType_UInt32) throw glare::Exception("Invalid native index type.");
	if(native.index_data.size()%BatchedMesh::componentTypeSize(native.index_type)!=0) throw glare::Exception("Invalid native index buffer.");
	const size_t count=native.numIndices();
	if(count==0||count%3!=0||count>120000) throw glare::Exception("UV editing requires 1..40000 native triangles.");
	for(size_t i=0;i<count;++i) if(native.getIndexAsUInt32(i)>=native.numVerts()) throw glare::Exception("Native UV input has an out-of-range vertex index.");
	std::vector<bool> covered(count/3,false);
	for(const auto& batch:native.batches) {
		if(batch.indices_start%3||batch.num_indices%3||batch.indices_start>count||batch.num_indices>count-batch.indices_start||batch.material_index>=10000) throw glare::Exception("Invalid native material batch.");
		for(size_t f=batch.indices_start/3;f<(size_t(batch.indices_start)+batch.num_indices)/3;++f) { if(covered[f]) throw glare::Exception("Native material batches overlap.");covered[f]=true; }
	}
	if(std::find(covered.begin(),covered.end(),false)!=covered.end()) throw glare::Exception("Native material batches do not cover every triangle.");
}

static double advancedHalfValue(const uint8* bytes)
{
	uint16 bits;std::memcpy(&bits,bytes,2);const unsigned exponent=(bits>>10)&31,mantissa=bits&1023;
	if(exponent==31) throw glare::Exception("UV input contains a non-finite half value.");
	const double value=exponent?std::ldexp(1.0+mantissa/1024.0,(int)exponent-15):std::ldexp((double)mantissa,-24);
	return bits&0x8000?-value:value;
}

static MCPMeshOps::Mesh advancedNativeUVInput(const BatchedMesh& native)
{
	advancedNativeUVLayout(native);const auto& position=native.getAttribute(BatchedMesh::VertAttribute_Position);const auto* uv=native.findAttribute(BatchedMesh::VertAttribute_UV_0);
	if(position.component_type!=BatchedMesh::ComponentType_Float&&position.component_type!=BatchedMesh::ComponentType_Half&&position.component_type!=BatchedMesh::ComponentType_UInt16) throw glare::Exception("Unsupported native position format for UV editing.");
	if(uv&&uv->component_type!=BatchedMesh::ComponentType_Float&&uv->component_type!=BatchedMesh::ComponentType_Half&&uv->component_type!=BatchedMesh::ComponentType_UInt16) throw glare::Exception("Unsupported native UV0 format.");
	const size_t stride=native.vertexSize();MCPMeshOps::Mesh mesh(native.numIndices()/3);
	for(const auto& batch:native.batches) for(size_t f=batch.indices_start/3;f<(size_t(batch.indices_start)+batch.num_indices)/3;++f) mesh[f].material=batch.material_index;
	for(size_t i=0;i<native.numIndices();++i) {
		const uint8* base=native.vertex_data.data()+native.getIndexAsUInt32(i)*stride;auto& v=mesh[i/3].vertices[i%3];
		for(int c=0;c<3;++c) {
			if(position.component_type==BatchedMesh::ComponentType_Float) { float value;std::memcpy(&value,base+position.offset_B+c*4,4);v.position[c]=value; }
			else if(position.component_type==BatchedMesh::ComponentType_Half) v.position[c]=advancedHalfValue(base+position.offset_B+c*2);
			else { uint16 value;std::memcpy(&value,base+position.offset_B+c*2,2);v.position[c]=(double)native.aabb_os.min_[c]+((double)native.aabb_os.max_[c]-native.aabb_os.min_[c])*(value/65535.0); }
		}
		if(uv) for(int c=0;c<2;++c) {
			double value;
			if(uv->component_type==BatchedMesh::ComponentType_Float) { float f;std::memcpy(&f,base+uv->offset_B+c*4,4);value=f; }
			else if(uv->component_type==BatchedMesh::ComponentType_Half) value=advancedHalfValue(base+uv->offset_B+c*2);
			else { uint16 q;std::memcpy(&q,base+uv->offset_B+c*2,2);if(!std::isfinite(native.uv0_scale)||native.uv0_scale<0) throw glare::Exception("Invalid native UV0 dequantisation scale.");value=q*(double)native.uv0_scale; }
			(c==0?v.u:v.v)=value;
		}
	}
	// Normals need no decoding: UV operations do not use them and the output copies
	// the authored bytes. Finite positions/UV0 are still validated before editing.
	try { MCPMeshOps::Detail::tolerance(mesh); } catch(const std::exception& e) { throw glare::Exception(e.what()); }
	return mesh;
}

static Reference<BatchedMesh> advancedNativeUVOutput(const BatchedMesh& native,const MCPMeshOps::Mesh& edited)
{
	advancedNativeUVLayout(native);if(edited.size()!=native.numIndices()/3) throw glare::Exception("UV editing must preserve the native triangle order and count.");
	// Split corners only in the native vertex buffer; retain all non-UV attributes,
	// animation data and material batches. Tangents are invalidated by UV changes.
	Reference<BatchedMesh> result=new BatchedMesh();result->animation_data=native.animation_data;result->batches=native.batches;result->aabb_os=native.aabb_os;result->uv0_scale=1;result->uv1_scale=native.uv1_scale;
	size_t stride=0;
	for(const auto& a:native.vert_attributes) {
		if(a.type==BatchedMesh::VertAttribute_UV_0||a.type==BatchedMesh::VertAttribute_Tangent) continue;
		if(result->vert_attributes.size()==1&&result->vert_attributes[0].type==BatchedMesh::VertAttribute_Position&&result->vert_attributes[0].component_type==BatchedMesh::ComponentType_UInt16&&a.type==BatchedMesh::VertAttribute_Normal&&a.component_type==BatchedMesh::ComponentType_Oct16) stride=6;
		result->vert_attributes.push_back(BatchedMesh::VertAttribute(a.type,a.component_type,stride));stride=(stride+BatchedMesh::vertAttributeSize(a)+3)&~size_t(3);
	}
	const size_t uv_offset=stride;result->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_UV_0,BatchedMesh::ComponentType_Float,stride));stride+=8;
	if(result->vertexSize()!=stride) throw glare::Exception("Invalid UV result attribute packing.");
	const size_t count=native.numIndices(),old_stride=native.vertexSize();result->vertex_data.resize(count*stride,0);js::Vector<uint32,16> indices;indices.resize(count);
	for(size_t i=0;i<count;++i) {
		const uint32 source=native.getIndexAsUInt32(i);indices[i]=(uint32)i;
		for(const auto& a:result->vert_attributes) if(a.type!=BatchedMesh::VertAttribute_UV_0) { const auto& old=native.getAttribute(a.type);std::memcpy(result->vertex_data.data()+i*stride+a.offset_B,native.vertex_data.data()+source*old_stride+old.offset_B,BatchedMesh::vertAttributeSize(a)); }
		const auto& v=edited[i/3].vertices[i%3];const float uv[2]={(float)v.u,(float)v.v};if(!std::isfinite(uv[0])||!std::isfinite(uv[1])) throw glare::Exception("UV result is outside float range.");std::memcpy(result->vertex_data.data()+i*stride+uv_offset,uv,sizeof(uv));
	}
	result->setIndexDataFromIndices(indices,count);return result;
}

#ifndef MCP_ADVANCED_NATIVE_UV_ONLY
static MCPMeshOps::Vec3 advancedVector(const JSONParser& p,const JSONNode& n,const char* key,const Vec3d& fallback)
{
	const Vec3d v=vector(p,n,key,fallback,-10000,10000);return MCPMeshOps::Vec3(v.x,v.y,v.z);
}

static std::string advancedMeshEdit(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user,const std::string& username,const std::string& tool)
{
	WorldObjectRef ob=meshSnapshot(all,w,p,n,user,"replace_uid");Reference<BatchedMesh> native=readNativeMesh(all,*ob);
	if(tool!="edit_mesh_uv") requireStaticEditableAttributes(*native);
	const MCPMeshOps::Mesh input=tool=="edit_mesh_uv"?advancedNativeUVInput(*native):operationsMesh(*native->buildIndigoMesh());MCPMeshOps::Mesh edited;
	try {
		if(tool=="subdivide_mesh") {
			MCPMeshOps::Subdivision op;op.mode=n.getChildStringValueWithDefaultVal(p,"mode","linear");op.levels=integer(p,n,"levels",1,1,5);op.preserve_boundaries=n.getChildBoolValueWithDefaultVal(p,"preserve_boundaries",true);
			edited=MCPMeshOps::subdivideMesh(input,op);
		} else if(tool=="remesh_mesh") {
			MCPMeshOps::Remesh op;op.target_edge_length=number(p,n,"target_edge_length",.1,.00001,10000);op.iterations=integer(p,n,"iterations",1,1,5);op.smooth=number(p,n,"smooth",0,0,.5);op.preserve_boundaries=n.getChildBoolValueWithDefaultVal(p,"preserve_boundaries",true);
			edited=MCPMeshOps::remeshMesh(input,op);
		} else if(tool=="sculpt_mesh") {
			MCPMeshOps::SculptStroke op;op.mode=n.getChildStringValue(p,"mode");op.direction=advancedVector(p,n,"direction",Vec3d(0,0,1));op.radius=number(p,n,"radius",1,.0001,10000);op.strength=number(p,n,"strength",.1,-1000,1000);op.iterations=integer(p,n,"iterations",1,1,10);
			op.front_faces_only=n.getChildBoolValueWithDefaultVal(p,"front_faces_only",false);op.preserve_boundaries=n.getChildBoolValueWithDefaultVal(p,"preserve_boundaries",true);
			const auto& samples=n.getChildArray(p,"samples");if(samples.child_indices.empty()||samples.child_indices.size()>128) throw glare::Exception("A stroke requires 1..128 samples.");
			for(uint32 index:samples.child_indices) { const auto& s=p.nodes[index];MCPMeshOps::SculptSample sample;sample.center=advancedVector(p,s,"center",Vec3d(0,0,0));sample.pressure=number(p,s,"pressure",1,0,1);op.samples.push_back(sample); }
			edited=MCPMeshOps::sculptMesh(input,op);
		} else if(tool=="edit_mesh_uv") {
			MCPMeshOps::UVEdit op;op.mode=n.getChildStringValue(p,"mode");op.axis=integer(p,n,"axis",2,0,2);op.rotation=number(p,n,"rotation",0,-1000,1000);
			for(const char* key:{"offset","scale","pivot"}) if(n.hasChild(key)) { double v[2];n.getChildArray(p,key).parseDoubleArrayValues(p,2,v);for(double x:v) if(!std::isfinite(x)||std::abs(x)>100000) throw glare::Exception("UV transform exceeds finite bounds.");auto& dst=std::string(key)=="offset"?op.offset:std::string(key)=="scale"?op.scale:op.pivot;dst={{v[0],v[1]}}; }
			if(n.hasChild("triangles")) { const auto& list=n.getChildArray(p,"triangles");if(list.child_indices.size()>40000) throw glare::Exception("UV selection exceeds 40000 faces.");for(uint32 i:list.child_indices) { const double f=p.nodes[i].getDoubleValue();if(!std::isfinite(f)||f<0||f>=input.size()||f!=std::floor(f)) throw glare::Exception("Invalid UV face index.");op.triangles.push_back((size_t)f); } }
			if(n.hasChild("corners")) { const auto& list=n.getChildArray(p,"corners");if(list.child_indices.size()>120000) throw glare::Exception("Too many UV corners.");for(uint32 i:list.child_indices) { const auto& c=p.nodes[i];MCPMeshOps::UVCorner corner;corner.triangle=integer(p,c,"triangle",-1,0,(int)input.size()-1);corner.corner=integer(p,c,"corner",-1,0,2);double uv[2];c.getChildArray(p,"uv").parseDoubleArrayValues(p,2,uv);corner.u=uv[0];corner.v=uv[1];op.corners.push_back(corner); } }
			edited=MCPMeshOps::editUV(input,op);
		} else throw glare::Exception("Unknown advanced mesh operation.");
	} catch(const glare::Exception&) { throw; } catch(const std::exception& e) { throw glare::Exception(e.what()); }
	Reference<BatchedMesh> result;
	if(tool=="edit_mesh_uv") result=advancedNativeUVOutput(*native,edited);
	else { MeshBuilder builder;builder.appendOperationsMesh(edited);builder.mesh.endOfModel();result=BatchedMesh::buildFromIndigoMesh(builder.mesh); }
	result->checkValidAndSanitiseMesh();ob->setAABBOS(result->aabb_os);const std::vector<WorldObjectRef> sources{ob};
	const std::string updated=publish(all,w,ob,p,n,user,username,result,&sources);
	return "{\"object\":"+updated+",\"triangles_before\":"+toString(input.size())+",\"triangles_after\":"+toString(edited.size())+",\"coordinates\":\"object_local\"}";
}
#endif
