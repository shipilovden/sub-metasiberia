// Included inside MCPHandlers::Modeling, after meshSnapshot/readNativeMesh/publish.
// Edits native vertex buffers directly: an Indigo round trip loses skin attributes.
#pragma once

static void rigLimits(const BatchedMesh& mesh)
{
	if(mesh.numVerts() == 0 || mesh.numVerts() > 200000) throw glare::Exception("Rigging supports 1..200000 native vertices.");
	if(mesh.animation_data.nodes.size() > 256 || mesh.animation_data.joint_nodes.size() > 256 || mesh.animation_data.animations.size() > 64)
		throw glare::Exception("Rig exceeds the MCP limit of 256 nodes/joints and 64 clips.");
	mesh.animation_data.checkDataIsValid();
}

static Quatf rigQuaternion(const JSONParser& p, const JSONNode& n, const char* key)
{
	double v[4] = {0,0,0,1};
	if(n.hasChild(key)) n.getChildArray(p,key).parseDoubleArrayValues(p,4,v);
	double norm = 0;
	for(double x : v) { if(!std::isfinite(x) || std::abs(x)>1e6) throw glare::Exception("Invalid quaternion."); norm += x*x; }
	if(norm < 1e-12) throw glare::Exception("Quaternion must have nonzero length.");
	const double inv = 1/std::sqrt(norm);
	return Quatf((float)(v[0]*inv),(float)(v[1]*inv),(float)(v[2]*inv),(float)(v[3]*inv));
}

static std::string rigVec4JSON(const Vec4f& v)
{
	return "["+doubleToString(v[0])+","+doubleToString(v[1])+","+doubleToString(v[2])+","+doubleToString(v[3])+"]";
}

static void rigReadWeights(const BatchedMesh& mesh, size_t vertex, uint16 joints[4], float weights[4])
{
	const auto* j = mesh.findAttribute(BatchedMesh::VertAttribute_Joints);
	const auto* w = mesh.findAttribute(BatchedMesh::VertAttribute_Weights);
	if(!j || !w) throw glare::Exception("Mesh has no skin weights; create a skeleton or auto-bind first.");
	const uint8* base = mesh.vertex_data.data()+vertex*mesh.vertexSize();
	for(int k=0;k<4;++k)
	{
		if(j->component_type==BatchedMesh::ComponentType_UInt8) joints[k]=base[j->offset_B+k];
		else if(j->component_type==BatchedMesh::ComponentType_UInt16) std::memcpy(&joints[k],base+j->offset_B+k*2,2);
		else throw glare::Exception("Unsupported joint attribute format.");
		if(w->component_type==BatchedMesh::ComponentType_Float) std::memcpy(&weights[k],base+w->offset_B+k*4,4);
		else if(w->component_type==BatchedMesh::ComponentType_UInt8) weights[k]=base[w->offset_B+k]/255.f;
		else if(w->component_type==BatchedMesh::ComponentType_UInt16) { uint16 value; std::memcpy(&value,base+w->offset_B+k*2,2); weights[k]=value/65535.f; }
		else throw glare::Exception("Unsupported weight attribute format.");
	}
}

// Repack only skin attributes. Keep normals, UVs, tangents, quantised positions,
// material batches and original vertex numbering byte-for-byte.
static void rigPrepareWeights(BatchedMesh& mesh, bool preserve, bool add_skin=true)
{
	const size_t vertices=mesh.numVerts(), old_stride=mesh.vertexSize();
	const auto old_attributes=mesh.vert_attributes;
	std::vector<std::array<uint16,4>> joints;
	std::vector<std::array<float,4>> weights;
	if(preserve)
	{
		joints.resize(vertices); weights.resize(vertices);
		for(size_t i=0;i<vertices;++i) rigReadWeights(mesh,i,joints[i].data(),weights[i].data());
	}
	std::vector<BatchedMesh::VertAttribute> attributes;
	size_t offset=0;
	for(const auto& a : old_attributes)
	{
		if(a.type==BatchedMesh::VertAttribute_Joints || a.type==BatchedMesh::VertAttribute_Weights) continue;
		if(attributes.size()==1 && attributes[0].type==BatchedMesh::VertAttribute_Position && attributes[0].component_type==BatchedMesh::ComponentType_UInt16 &&
			a.type==BatchedMesh::VertAttribute_Normal && a.component_type==BatchedMesh::ComponentType_Oct16) offset=6;
		attributes.push_back(BatchedMesh::VertAttribute(a.type,a.component_type,offset));
		offset=(offset+BatchedMesh::vertAttributeSize(a)+3)&~size_t(3);
	}
	const size_t joint_offset=offset, weight_offset=offset+8, stride=offset+(add_skin?24:0);
	if(add_skin)
	{
		attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Joints,BatchedMesh::ComponentType_UInt16,joint_offset));
		attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Weights,BatchedMesh::ComponentType_Float,weight_offset));
	}
	glare::AllocatorVector<uint8,16> data; data.resize(vertices*stride,0);
	for(size_t i=0;i<vertices;++i)
	{
		for(const auto& a : attributes)
		{
			if(a.type==BatchedMesh::VertAttribute_Joints || a.type==BatchedMesh::VertAttribute_Weights) continue;
			for(const auto& old : old_attributes) if(old.type==a.type)
				std::memcpy(data.data()+i*stride+a.offset_B,mesh.vertex_data.data()+i*old_stride+old.offset_B,BatchedMesh::vertAttributeSize(a));
		}
		if(preserve) { std::memcpy(data.data()+i*stride+joint_offset,joints[i].data(),8); std::memcpy(data.data()+i*stride+weight_offset,weights[i].data(),16); }
	}
	mesh.vert_attributes=attributes;
	mesh.vertex_data.swapWith(data);
}

static void rigWriteWeights(BatchedMesh& mesh, size_t vertex, const uint16 joints[4], const float weights[4])
{
	uint8* base=mesh.vertex_data.data()+vertex*mesh.vertexSize();
	std::memcpy(base+mesh.getAttribute(BatchedMesh::VertAttribute_Joints).offset_B,joints,8);
	std::memcpy(base+mesh.getAttribute(BatchedMesh::VertAttribute_Weights).offset_B,weights,16);
}

static void rigAutoBind(BatchedMesh& mesh, int influences)
{
	const auto& data=mesh.animation_data;
	if(data.joint_nodes.empty()) throw glare::Exception("Create a skeleton before binding weights.");
	if(mesh.getAttribute(BatchedMesh::VertAttribute_Position).component_type!=BatchedMesh::ComponentType_Float)
		throw glare::Exception("Auto-binding requires native float positions; convert the quantised mesh before creating a rig.");
	std::vector<Vec4f> starts(data.joint_nodes.size()), ends(data.joint_nodes.size());
	for(size_t j=0;j<data.joint_nodes.size();++j)
	{
		const int node=data.joint_nodes[j];
		starts[j]=ends[j]=data.getNodePositionModelSpace(node,false);
		// A joint influences the segment toward its first child, or its own origin for a leaf.
		for(int child : data.joint_nodes) if(data.nodes[child].parent_index==node) { ends[j]=data.getNodePositionModelSpace(child,false); break; }
	}
	rigPrepareWeights(mesh,false);
	for(size_t i=0;i<mesh.numVerts();++i)
	{
		const Vec4f pos=mesh.getVertexPosition((uint32)i);
		std::array<std::pair<double,uint16>,4> best;
		for(auto& b : best) b=std::make_pair(1e100,(uint16)0);
		for(size_t j=0;j<starts.size();++j)
		{
			double len2=0, proj=0;
			for(int k=0;k<3;++k) { const double d=ends[j][k]-starts[j][k]; len2+=d*d; proj+=(pos[k]-starts[j][k])*d; }
			const double t=len2>1e-12?std::max(0.0,std::min(1.0,proj/len2)):0;
			double distance=0;
			for(int k=0;k<3;++k) { const double d=pos[k]-(starts[j][k]+t*(ends[j][k]-starts[j][k])); distance+=d*d; }
			const std::pair<double,uint16> candidate(distance,(uint16)j);
			for(int k=0;k<influences;++k) if(candidate<best[k]) { for(int q=influences-1;q>k;--q) best[q]=best[q-1]; best[k]=candidate; break; }
		}
		uint16 joints[4]={0,0,0,0}; float weights[4]={0,0,0,0}; double sum=0;
		for(int k=0;k<influences && k<(int)starts.size();++k) { joints[k]=best[k].second; weights[k]=(float)(1/std::max(1e-8,best[k].first)); sum+=weights[k]; }
		for(float& weight : weights) weight=(float)(weight/sum);
		rigWriteWeights(mesh,i,joints,weights);
	}
}

static std::string rigPublish(ServerAllWorldsState& all, ServerWorldState& w, const JSONParser& p, const JSONNode& n,
	const UserID& user, const std::string& username, WorldObjectRef ob, const Reference<BatchedMesh>& mesh)
{
	if(n.hasChild("consume_uid") || n.hasChild("operand_uid")) throw glare::Exception("Rig edits cannot consume another object.");
	mesh->animation_data.checkDataIsValid();
	mesh->checkValidAndSanitiseMesh();
	const std::vector<WorldObjectRef> sources{ob};
	return publish(all,w,ob,p,n,user,username,mesh,&sources);
}

static std::string createSkeleton(ServerAllWorldsState& all, ServerWorldState& w, const JSONParser& p, const JSONNode& n,
	const UserID& user, const std::string& username)
{
	WorldObjectRef ob=meshSnapshot(all,w,p,n,user,"replace_uid");
	Reference<BatchedMesh> mesh=readNativeMesh(all,*ob); rigLimits(*mesh);
	if(n.getChildBoolValueWithDefaultVal(p,"remove",false))
	{
		AnimationData empty; mesh->animation_data=empty; rigPrepareWeights(*mesh,false,false);
		return rigPublish(all,w,p,n,user,username,ob,mesh);
	}
	if(!mesh->animation_data.nodes.empty() && !n.getChildBoolValueWithDefaultVal(p,"replace_existing",false))
		throw glare::Exception("Mesh already has a skeleton. Inspect it first; replace_existing=true explicitly discards the old rig and clips.");
	const auto& bones=n.getChildArray(p,"bones");
	if(bones.child_indices.empty() || bones.child_indices.size()>256) throw glare::Exception("Use 1..256 bones in parent-before-child order.");
	AnimationData rig;
	std::vector<Matrix4f> global;
	std::set<std::string> names;
	for(uint32 id : bones.child_indices)
	{
		const auto& bone=p.nodes[id]; AnimationNodeData node;
		node.name=bone.getChildStringValue(p,"name");
		if(node.name.empty() || node.name.size()>128 || !names.insert(node.name).second) throw glare::Exception("Bone names must be unique and 1..128 bytes.");
		node.parent_index=integer(p,bone,"parent",-1,-1,(int)rig.nodes.size()-1);
		const Vec3d t=vector(p,bone,"translation",Vec3d(0,0,0),-10000,10000), s=vector(p,bone,"scale",Vec3d(1,1,1),.001,1000);
		node.trans=Vec4f((float)t.x,(float)t.y,(float)t.z,0); node.scale=Vec4f((float)s.x,(float)s.y,(float)s.z,0);
		node.rot=rigQuaternion(p,bone,"rotation");
		const Matrix4f local=Matrix4f::translationMatrix(node.trans)*node.rot.toMatrix()*Matrix4f::scaleMatrix((float)s.x,(float)s.y,(float)s.z);
		const Matrix4f world=node.parent_index<0?local:global[node.parent_index]*local;
		for(float component : world.e) if(!std::isfinite(component) || std::abs(component)>1e8f) throw glare::Exception("Accumulated bone transform exceeds supported range.");
		if(!world.getInverseForAffine3Matrix(node.inverse_bind_matrix)) throw glare::Exception("Bone bind transform is singular.");
		for(float component : node.inverse_bind_matrix.e) if(!std::isfinite(component) || std::abs(component)>1e8f) throw glare::Exception("Inverse bone transform exceeds supported range.");
		global.push_back(world); rig.sorted_nodes.push_back((int)rig.nodes.size()); rig.joint_nodes.push_back((int)rig.nodes.size()); rig.nodes.push_back(node);
	}
	mesh->animation_data=rig;
	rigAutoBind(*mesh,integer(p,n,"influences",4,1,4));
	return rigPublish(all,w,p,n,user,username,ob,mesh);
}

static std::string setSkinWeights(ServerAllWorldsState& all, ServerWorldState& w, const JSONParser& p, const JSONNode& n,
	const UserID& user, const std::string& username)
{
	WorldObjectRef ob=meshSnapshot(all,w,p,n,user,"replace_uid");
	Reference<BatchedMesh> mesh=readNativeMesh(all,*ob); rigLimits(*mesh);
	if(mesh->animation_data.joint_nodes.empty()) throw glare::Exception("Create a skeleton first.");
	const std::string mode=n.getChildStringValueWithDefaultVal(p,"mode","auto");
	if(mode=="auto") rigAutoBind(*mesh,integer(p,n,"influences",4,1,4));
	else if(mode=="explicit")
	{
		rigPrepareWeights(*mesh,true);
		const auto& assignments=n.getChildArray(p,"assignments");
		if(assignments.child_indices.empty() || assignments.child_indices.size()>10000) throw glare::Exception("Use 1..10000 vertex weight assignments per call.");
		std::set<int> seen;
		for(uint32 id : assignments.child_indices)
		{
			const auto& row=p.nodes[id];
			if(!row.hasChild("vertex")) throw glare::Exception("Each assignment needs its native vertex index from get_rig(section=weights).");
			const int vertex=integer(p,row,"vertex",0,0,(int)mesh->numVerts()-1);
			if(!seen.insert(vertex).second) throw glare::Exception("Duplicate vertex weight assignment.");
			const auto& ja=row.getChildArray(p,"joints"); const auto& wa=row.getChildArray(p,"weights");
			if(ja.child_indices.empty() || ja.child_indices.size()>4 || ja.child_indices.size()!=wa.child_indices.size()) throw glare::Exception("Use 1..4 matching joints and weights.");
			double raw_j[4]={0,0,0,0}, raw_w[4]={0,0,0,0};
			ja.parseDoubleArrayValues(p,ja.child_indices.size(),raw_j); wa.parseDoubleArrayValues(p,wa.child_indices.size(),raw_w);
			double sum=0; uint16 joints[4]={0,0,0,0}; float weights[4]={0,0,0,0}; std::set<int> used;
			for(size_t k=0;k<ja.child_indices.size();++k)
			{
				if(!std::isfinite(raw_j[k]) || raw_j[k]<0 || raw_j[k]>=mesh->animation_data.joint_nodes.size() || raw_j[k]!=std::floor(raw_j[k]) || !used.insert((int)raw_j[k]).second)
					throw glare::Exception("Invalid or repeated joint index.");
				if(!std::isfinite(raw_w[k]) || raw_w[k]<0 || raw_w[k]>1) throw glare::Exception("Weights must be finite and in 0..1.");
				joints[k]=(uint16)raw_j[k]; sum+=raw_w[k];
			}
			if(sum<1e-12) throw glare::Exception("At least one weight must be positive.");
			for(int k=0;k<4;++k) weights[k]=(float)(raw_w[k]/sum);
			rigWriteWeights(*mesh,(size_t)vertex,joints,weights);
		}
	}
	else throw glare::Exception("Weight mode must be auto or explicit.");
	return rigPublish(all,w,p,n,user,username,ob,mesh);
}

// Compact accessors when a clip is replaced, avoiding unbounded accumulation.
static void rigCompactClips(AnimationData& data)
{
	js::Vector<KeyFrameTimeInfo> times;
	js::Vector<js::Vector<Vec4f,16>> values;
	size_t total_keys=0;
	for(auto& ref : data.animations)
	{
		AnimationDatum& clip=*ref;
		const auto& source_times=clip.m_keyframe_times.empty()?data.keyframe_times:clip.m_keyframe_times;
		const auto& source_values=clip.m_output_data.empty()?data.output_data:clip.m_output_data;
		for(auto& channel : clip.raw_per_anim_node_data)
		{
			int* inputs[]={&channel.translation_input_accessor,&channel.rotation_input_accessor,&channel.scale_input_accessor};
			int* outputs[]={&channel.translation_output_accessor,&channel.rotation_output_accessor,&channel.scale_output_accessor};
			for(int k=0;k<3;++k) if(*inputs[k]>=0)
			{
				total_keys+=source_times[*inputs[k]].times.size();
				if(total_keys>200000) throw glare::Exception("Total clip keyframes exceed 200000.");
				times.push_back(source_times[*inputs[k]]); values.push_back(source_values[*outputs[k]]);
				*inputs[k]=(int)times.size()-1; *outputs[k]=(int)values.size()-1;
			}
		}
		clip.m_keyframe_times.clear(); clip.m_output_data.clear(); clip.used_input_accessor_indices.clear();
	}
	data.keyframe_times=times; data.output_data=values;
	data.build(); data.checkDataIsValid(); data.checkPerAnimNodeDataIsValid();
}

static std::string setAnimationClip(ServerAllWorldsState& all, ServerWorldState& w, const JSONParser& p, const JSONNode& n,
	const UserID& user, const std::string& username)
{
	WorldObjectRef ob=meshSnapshot(all,w,p,n,user,"replace_uid");
	Reference<BatchedMesh> mesh=readNativeMesh(all,*ob); rigLimits(*mesh);
	AnimationData& data=mesh->animation_data;
	if(data.nodes.empty()) throw glare::Exception("Create a skeleton first.");
	const std::string name=n.getChildStringValue(p,"name");
	if(name.empty() || name.size()>128) throw glare::Exception("Clip name must be 1..128 bytes.");
	const int existing=data.getAnimationIndex(name);
	if(n.getChildBoolValueWithDefaultVal(p,"remove",false))
	{
		if(existing<0) throw glare::Exception("Clip does not exist.");
		data.animations.erase(data.animations.begin()+existing);
	}
	else
	{
		if(existing<0 && data.animations.size()>=64) throw glare::Exception("At most 64 clips are supported.");
		const auto& tracks=n.getChildArray(p,"tracks");
		if(tracks.child_indices.empty() || tracks.child_indices.size()>768) throw glare::Exception("Use 1..768 bone tracks.");
		Reference<AnimationDatum> clip=new AnimationDatum(); clip->name=name; clip->raw_per_anim_node_data.resize(data.nodes.size());
		for(auto& row : clip->raw_per_anim_node_data) row.init();
		std::set<std::pair<int,std::string>> seen; size_t key_count=0;
		for(uint32 id : tracks.child_indices)
		{
			const auto& track=p.nodes[id];
			if(!track.hasChild("bone")) throw glare::Exception("Each track needs a bone node index from get_rig.");
			const int bone=integer(p,track,"bone",0,0,(int)data.nodes.size()-1);
			const std::string channel=track.getChildStringValue(p,"channel");
			if(channel!="translation" && channel!="rotation" && channel!="scale") throw glare::Exception("Track channel must be translation, rotation or scale.");
			if(!seen.insert(std::make_pair(bone,channel)).second) throw glare::Exception("Duplicate bone channel.");
			const auto& ta=track.getChildArray(p,"times"); const auto& va=track.getChildArray(p,"values");
			if(ta.child_indices.size()<2 || ta.child_indices.size()>4096 || ta.child_indices.size()!=va.child_indices.size()) throw glare::Exception("A track needs 2..4096 matching times and values.");
			key_count+=ta.child_indices.size(); if(key_count>200000) throw glare::Exception("Clip keyframes exceed 200000.");
			std::vector<double> raw_times(ta.child_indices.size()); ta.parseDoubleArrayValues(p,raw_times.size(),raw_times.data());
			KeyFrameTimeInfo times; js::Vector<Vec4f,16> values;
			for(size_t k=0;k<raw_times.size();++k)
			{
				const double time=raw_times[k]; const float ft=(float)time;
				if(!std::isfinite(time) || time<0 || time>3600 || (k==0 && time!=0) || (k>0 && ft<=times.times.back())) throw glare::Exception("Times must start at 0 and strictly increase, up to 3600 seconds.");
				times.times.push_back(ft);
				double value[4]={0,0,0,0}; p.nodes[va.child_indices[k]].parseDoubleArrayValues(p,channel=="rotation"?4:3,value);
				for(int c=0;c<(channel=="rotation"?4:3);++c) if(!std::isfinite(value[c]) || std::abs(value[c])>10000 || (channel=="scale" && (value[c]<.001 || value[c]>1000))) throw glare::Exception("Invalid animation value.");
				if(channel=="rotation")
				{
					double len=0; for(double v : value) len+=v*v;
					if(len<1e-12) throw glare::Exception("Rotation keyframe must have nonzero quaternion.");
					for(double& v : value) v/=std::sqrt(len);
					// Keep adjacent quaternion keys in the same hemisphere for stable interpolation.
					if(!values.empty()) { double dot=0; for(int c=0;c<4;++c) dot+=values.back()[c]*value[c]; if(dot<0) for(double& v : value) v=-v; }
				}
				values.push_back(Vec4f((float)value[0],(float)value[1],(float)value[2],(float)value[3]));
			}
			const int accessor=(int)clip->m_keyframe_times.size(); clip->m_keyframe_times.push_back(times); clip->m_output_data.push_back(values);
			auto& row=clip->raw_per_anim_node_data[bone];
			if(channel=="translation") { row.translation_input_accessor=accessor; row.translation_output_accessor=accessor; }
			else if(channel=="rotation") { row.rotation_input_accessor=accessor; row.rotation_output_accessor=accessor; }
			else { row.scale_input_accessor=accessor; row.scale_output_accessor=accessor; }
		}
		if(existing>=0) data.animations[existing]=clip; else data.animations.push_back(clip);
		if(n.getChildBoolValueWithDefaultVal(p,"make_default",false))
		{
			const int index=data.getAnimationIndex(name); auto selected=data.animations[index]; data.animations.erase(data.animations.begin()+index); data.animations.insert(data.animations.begin(),selected);
		}
	}
	rigCompactClips(data);
	return rigPublish(all,w,p,n,user,username,ob,mesh);
}

static std::string getRig(ServerAllWorldsState& all, ServerWorldState& w, const JSONParser& p, const JSONNode& n, const UserID& user)
{
	WorldObjectRef ob=meshSnapshot(all,w,p,n,user); Reference<BatchedMesh> mesh=readNativeMesh(all,*ob); rigLimits(*mesh);
	const auto& data=mesh->animation_data;
	std::string out="{\"uid\":"+toString(ob->uid.value())+",\"vertex_count\":"+toString(mesh->numVerts())+",\"nodes\":[";
	for(size_t i=0;i<data.nodes.size();++i)
	{
		const auto& node=data.nodes[i]; if(i) out+=",";
		out+="{\"index\":"+toString(i)+",\"name\":\""+web::Escaping::JSONEscape(node.name)+"\",\"parent\":"+toString(node.parent_index)+
			",\"translation\":"+vecJSON(Vec3d(node.trans[0],node.trans[1],node.trans[2]))+",\"rotation\":"+rigVec4JSON(node.rot.v)+
			",\"scale\":"+vecJSON(Vec3d(node.scale[0],node.scale[1],node.scale[2]))+",\"inverse_bind_matrix\":[";
		for(int j=0;j<16;++j) { if(j) out+=","; out+=doubleToString(node.inverse_bind_matrix.e[j]); } out+="]}";
	}
	out+="],\"joint_nodes\":[";
	for(size_t i=0;i<data.joint_nodes.size();++i) { if(i) out+=","; out+=toString(data.joint_nodes[i]); }
	out+="],\"clips\":[";
	for(size_t i=0;i<data.animations.size();++i) { if(i) out+=","; out+="{\"index\":"+toString(i)+",\"name\":\""+web::Escaping::JSONEscape(data.animations[i]->name)+"\",\"duration\":"+doubleToString(data.animations[i]->anim_len)+"}"; }
	out+="]";
	const std::string section=n.getChildStringValueWithDefaultVal(p,"section","skeleton");
	const size_t offset=(size_t)integer(p,n,"offset",0,0,200000), limit=(size_t)integer(p,n,"limit",100,1,1000);
	if(section=="weights")
	{
		if(mesh->getAttribute(BatchedMesh::VertAttribute_Position).component_type!=BatchedMesh::ComponentType_Float)
			throw glare::Exception("Vertex inspection requires native float positions.");
		out+=",\"vertices\":["; const size_t end=std::min(mesh->numVerts(),offset+limit);
		for(size_t i=offset;i<end;++i)
		{
			uint16 joints[4]; float weights[4]; rigReadWeights(*mesh,i,joints,weights);
			const Vec4f pos=mesh->getVertexPosition((uint32)i); if(i!=offset) out+=",";
			out+="{\"vertex\":"+toString(i)+",\"position\":"+vecJSON(Vec3d(pos[0],pos[1],pos[2]))+",\"joints\":[";
			for(int k=0;k<4;++k) { if(k) out+=","; out+=toString(joints[k]); } out+="],\"weights\":[";
			for(int k=0;k<4;++k) { if(k) out+=","; out+=doubleToString(weights[k]); } out+="]}";
		}
		out+="],\"total\":"+toString(mesh->numVerts());
	}
	else if(section=="clip")
	{
		const std::string name=n.getChildStringValue(p,"clip_name"); const AnimationDatum* clip=nullptr;
		for(const auto& c : data.animations) if(c->name==name) clip=c.ptr();
		if(!clip) throw glare::Exception("Clip does not exist.");
		const auto& times=clip->m_keyframe_times.empty()?data.keyframe_times:clip->m_keyframe_times;
		const auto& values=clip->m_output_data.empty()?data.output_data:clip->m_output_data;
		out+=",\"tracks\":["; bool comma=false; size_t track_index=0;
		const size_t track_offset=(size_t)integer(p,n,"track_offset",0,0,768), track_limit=(size_t)integer(p,n,"track_limit",8,1,16);
		const char* names[]={"translation","rotation","scale"};
		for(size_t bone=0;bone<clip->raw_per_anim_node_data.size();++bone)
		{
			const auto& row=clip->raw_per_anim_node_data[bone]; const int inputs[]={row.translation_input_accessor,row.rotation_input_accessor,row.scale_input_accessor};
			const int outputs[]={row.translation_output_accessor,row.rotation_output_accessor,row.scale_output_accessor};
			for(int c=0;c<3;++c) if(inputs[c]>=0)
			{
				const size_t index=track_index++;
				if(index<track_offset || index>=track_offset+track_limit) continue;
				if(comma) out+=","; comma=true; const auto& t=times[inputs[c]].times; const auto& v=values[outputs[c]];
				out+="{\"bone\":"+toString(bone)+",\"channel\":\""+names[c]+"\",\"total\":"+toString(t.size())+",\"offset\":"+toString(offset)+",\"times\":[";
				const size_t end=std::min(t.size(),offset+std::min(limit,size_t(128)));
				for(size_t k=offset;k<end;++k) { if(k!=offset) out+=","; out+=doubleToString(t[k]); } out+="],\"values\":[";
				for(size_t k=offset;k<end;++k) { if(k!=offset) out+=","; out+=(c==1?rigVec4JSON(v[k]):vecJSON(Vec3d(v[k][0],v[k][1],v[k][2]))); } out+="]}";
			}
		}
		out+="],\"track_total\":"+toString(track_index)+",\"track_offset\":"+toString(track_offset);
	}
	else if(section!="skeleton") throw glare::Exception("section must be skeleton, weights or clip.");
	return out+"}";
}
