#include "../../gui_client/GestureAnimationRetarget.h"
#include <utils/FileInStream.h>
#include <utils/BufferOutStream.h>
#include <utils/BufferViewInStream.h>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool v,const char* why) { if(!v) throw std::runtime_error(why); }
Reference<AnimationData> read(const std::string& path) {
	FileInStream file(path); char magic[4]; file.readData(magic,4);
	Reference<AnimationData> result = new AnimationData(); result->readFromStream(file); return result;
}
std::vector<Matrix4f> pose(const AnimationData& data,int anim,float t) {
	const auto& clip=*data.animations[anim];
	std::vector<Matrix4f> result(data.nodes.size());
	auto sample=[&](int input,int output,const Vec4f& fallback,bool rotation) {
		if(input<0) return fallback;
		const auto& times=(clip.m_keyframe_times.empty()?data.keyframe_times:clip.m_keyframe_times)[input].times;
		const auto& values=(clip.m_output_data.empty()?data.output_data:clip.m_output_data)[output];
		auto it=std::upper_bound(times.begin(),times.end(),t);
		if(it==times.begin()) return values[0];
		if(it==times.end()) return values[values.size()-1];
		size_t k=it-times.begin(); float f=(t-times[k-1])/(times[k]-times[k-1]);
		return rotation?Quatf::nlerp(Quatf(values[k-1]),Quatf(values[k]),f).v:values[k-1]*(1-f)+values[k]*f;
	};
	for(int i:data.sorted_nodes) {
		const auto& n=data.nodes[i]; const auto& c=data.per_anim_node_data[anim][i];
		auto trans=sample(c.translation_input_accessor,c.translation_output_accessor,n.trans,false);
		auto scale=sample(c.scale_input_accessor,c.scale_output_accessor,n.scale,false);
		auto rot=Quatf(sample(c.rotation_input_accessor,c.rotation_output_accessor,n.rot.v,true)).toMatrix();
		Matrix4f local(rot.getColumn(0)*scale[0],rot.getColumn(1)*scale[1],rot.getColumn(2)*scale[2],setWToOne(trans));
		result[i]=n.parent_index<0?local:result[n.parent_index]*n.retarget_adjustment*local;
	}
	return result;
}
}

void testOrientation(const std::string& idle_path)
{
	const auto driver=read(idle_path), source=read(idle_path);
	check(normaliseGestureAnimation(*source,*driver).isNull(),"compatible Idle must not be resampled");
	const int hips=source->getNodeIndex("Hips"); check(hips>=0,"hips");
	// Same world-space motion, exported with a -90 degree helper parent and
	// inverse +90 degree Hips channels. Name-only append drops the helper.
	const Quatf turn=Quatf::fromAxisAndAngle(Vec4f(1,0,0,0),1.57079632679f);
	const int wrapper=int(source->nodes.size());
	AnimationNodeData node; node.name="ExporterAxis"; node.parent_index=source->nodes[hips].parent_index;
	node.trans=Vec4f(0); node.rot=turn.inverse(); node.scale=Vec4f(1,1,1,0); node.inverse_bind_matrix=Matrix4f::identity();
	source->nodes.push_back(node); source->nodes[hips].parent_index=wrapper;
	source->nodes[hips].rot=turn*source->nodes[hips].rot;
	source->nodes[hips].trans=turn.rotateVector(source->nodes[hips].trans);
	auto& c=source->animations[0]->raw_per_anim_node_data[hips];
	for(auto& r:source->output_data[c.rotation_output_accessor]) r=(turn*Quatf(r)).v;
	for(auto& p:source->output_data[c.translation_output_accessor]) p=turn.rotateVector(p);
	PerAnimationNodeData empty; empty.init(); source->animations[0]->raw_per_anim_node_data.push_back(empty);
	source->sorted_nodes.insert(std::find(source->sorted_nodes.begin(),source->sorted_nodes.end(),hips),wrapper);
	source->animations[0]->used_input_accessor_indices.clear(); source->build();
	const auto normalised=normaliseGestureAnimation(*source,*driver);
	check(normalised.nonNull(),"exporter frame change must be detected");
	for(const auto& values : normalised->output_data)
		if(values.size()==2) check(values.capacity()==2,"constant tracks retain oversized allocations");
	check(normaliseGestureAnimation(*normalised,*driver).isNull(),"normalization must be idempotent");
	// Serialize and reload: adjustments must be baked into tracks, not lost with SUBA.
	BufferOutStream encoded; normalised->writeToStream(encoded);
	BufferViewInStream input(ArrayRef<uint8>(encoded.buf.data(),encoded.buf.size()));
	AnimationData reloaded; reloaded.readFromStream(input); reloaded.prepareForMultipleUse();
	source->prepareForMultipleUse(); driver->prepareForMultipleUse();
	AnimationData world, preview, broken;
	for(auto* target:{&world,&preview,&broken}) { target->nodes=driver->nodes; target->sorted_nodes=driver->sorted_nodes; target->joint_nodes=driver->joint_nodes; }
	preview.loadAndRetargetAnim(*source);
	world.loadAndRetargetAnim(*driver); world.appendAnimationData(reloaded);
	broken.loadAndRetargetAnim(*driver); broken.appendAnimationData(*source);
	bool reproduced=false;
	for(float fraction:{0.f,.25f,.5f,.9f}) {
		const float t=fraction*source->animations[0]->anim_len;
		const auto expected=pose(preview,0,t), actual=pose(world,1,t), old=pose(broken,1,t);
		for(size_t j=0;j<driver->joint_nodes.size();++j) {
			const int a=preview.joint_nodes[j], b=world.joint_nodes[j], d=broken.joint_nodes[j];
			const auto expected_skin=expected[a]*preview.nodes[a].inverse_bind_matrix;
			const auto actual_skin=actual[b]*world.nodes[b].inverse_bind_matrix;
			const auto old_skin=old[d]*broken.nodes[d].inverse_bind_matrix;
			for(int e=0;e<16;++e) {
				check(std::abs(expected_skin.e[e]-actual_skin.e[e])<.005f,"world differs from reference preview after normalization");
				if(std::abs(expected_skin.e[e]-old_skin.e[e])>.1f) reproduced=true;
			}
		}
	}
	check(reproduced,"test did not reproduce tilted world playback");
	std::cout<<"PASS: reproduced 90-degree world mismatch; canonical tracks match preview after serialization, idempotent\n";
}
