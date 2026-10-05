#include "GestureAnimationRetarget.h"
#include <utils/Exception.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace {
void check(bool value, const char* message) { if(!value) throw glare::Exception(message); }
Matrix4f trs(const Vec4f& translation, const Quatf& rotation, const Vec4f& scale)
{
	const auto r = rotation.toMatrix();
	return Matrix4f(r.getColumn(0)*scale[0], r.getColumn(1)*scale[1], r.getColumn(2)*scale[2], setWToOne(translation));
}
bool close(const Matrix4f& a, const Matrix4f& b)
{
	for(int i=0; i<16; ++i) if(std::abs(a.e[i]-b.e[i]) > 0.0001f) return false;
	return true;
}
bool compatible(const AnimationData& a, const AnimationData& b)
{
	if(a.nodes.size() != b.nodes.size()) return false;
	for(const auto& n : a.nodes) {
		const int j = b.getNodeIndex(n.name);
		if(j < 0) return false;
		const auto& m = b.nodes[j];
		if((n.parent_index < 0) != (m.parent_index < 0)) return false;
		if(n.parent_index >= 0 && a.nodes[n.parent_index].name != b.nodes[m.parent_index].name) return false;
		if(!close(trs(n.trans,n.rot,n.scale), trs(m.trans,m.rot,m.scale)) || !close(n.inverse_bind_matrix,m.inverse_bind_matrix)) return false;
	}
	return true;
}
Vec4f sample(const AnimationData& data, const AnimationDatum& clip, int input, int output, float t, const Vec4f& fallback, bool rotation)
{
	if(input < 0) return fallback;
	const auto& times = (clip.m_keyframe_times.empty() ? data.keyframe_times : clip.m_keyframe_times)[input].times;
	const auto& values = (clip.m_output_data.empty() ? data.output_data : clip.m_output_data)[output];
	const auto next = std::upper_bound(times.begin(), times.end(), t);
	if(next == times.begin()) return values[0];
	if(next == times.end()) return values[values.size()-1];
	const size_t k = size_t(next-times.begin());
	const float u = (t-times[k-1]) / (times[k]-times[k-1]);
	return rotation ? Quatf::nlerp(Quatf(values[k-1]), Quatf(values[k]), u).v : values[k-1]*(1-u)+values[k]*u;
}
}

Reference<AnimationData> normaliseGestureAnimation(const AnimationData& source, const AnimationData& driver, const std::function<bool()>& cancelled)
{
	source.checkDataIsValid(); driver.checkDataIsValid();
	if(compatible(source, driver)) return nullptr;
	check(source.animations.size() == 1 && source.nodes.size() <= 512 && driver.nodes.size() <= 512, "Unsupported gesture skeleton/clip count.");
	const float duration = source.animations[0]->anim_len;
	check(std::isfinite(duration) && duration > 0 && duration <= 3600, "Invalid gesture duration.");
	// Reuse the same retargeting as the preview, then bake its skin transforms
	// onto the canonical bones. Merely serializing 'retarget_adjustment' loses it:
	// SUBA deliberately stores rest TRS and bind matrices, not per-avatar adjustments.
	AnimationData retargeted;
	retargeted.nodes = driver.nodes; retargeted.sorted_nodes = driver.sorted_nodes; retargeted.joint_nodes = driver.joint_nodes;
	retargeted.loadAndRetargetAnim(source);
	const auto& clip = *retargeted.animations[0];
	std::set<float> keys{0.f, duration};
	const auto& inputs = clip.m_keyframe_times.empty() ? retargeted.keyframe_times : clip.m_keyframe_times;
	for(const auto& input : inputs) for(float t : input.times) {
		check(std::isfinite(t) && t >= 0 && t <= duration, "Invalid gesture key time.");
		keys.insert(t);
		check(keys.size() <= 100000, "Gesture has too many key times.");
	}
	const int intervals = std::max(1, int(std::ceil(duration*60.0)));
	for(int i=1; i<intervals; ++i) keys.insert(float(double(duration)*i/intervals));
	check(keys.size() <= 100000 && keys.size() * (driver.nodes.size()*3+1) <= 2000000, "Gesture retargeting exceeds sample budget.");
	Reference<AnimationData> result = new AnimationData();
	result->nodes = driver.nodes; result->sorted_nodes = driver.sorted_nodes; result->joint_nodes = driver.joint_nodes;
	for(auto& node : result->nodes) node.retarget_adjustment = Matrix4f::identity();
	Reference<AnimationDatum> out_clip = new AnimationDatum();
	out_clip->name = clip.name; out_clip->raw_per_anim_node_data.resize(driver.nodes.size());
	result->keyframe_times.resize(1); result->keyframe_times[0].times.assign(keys.begin(),keys.end());
	result->output_data.resize(driver.nodes.size()*3);
	std::vector<int> mapping(driver.nodes.size(), -1);
	std::vector<Matrix4f> bind(driver.nodes.size());
	// Joint order is retained by loadAndRetargetAnim, even when node order changes.
	for(size_t j=0; j<driver.joint_nodes.size(); ++j) {
		const int target = driver.joint_nodes[j];
		mapping[target] = retargeted.joint_nodes[j];
		check(driver.nodes[target].inverse_bind_matrix.getInverseForAffine3Matrix(bind[target]), "Singular gesture bind matrix.");
	}
	for(size_t i=0; i<driver.nodes.size(); ++i) {
		auto& c = out_clip->raw_per_anim_node_data[i];
		c.translation_input_accessor=c.rotation_input_accessor=c.scale_input_accessor=0;
		c.translation_output_accessor=int(i*3); c.rotation_output_accessor=int(i*3+1); c.scale_output_accessor=int(i*3+2);
		for(int v=0; v<3; ++v) result->output_data[i*3+v].reserve(keys.size());
	}
	std::vector<Matrix4f> world(retargeted.nodes.size()), target_world(driver.nodes.size());
	for(float t : keys) {
		check(!cancelled || !cancelled(), "Animation import cancelled.");
		for(int i : retargeted.sorted_nodes) {
			const auto& n = retargeted.nodes[i]; const auto& c = retargeted.per_anim_node_data[0][i];
			const auto local = trs(sample(retargeted,clip,c.translation_input_accessor,c.translation_output_accessor,t,n.trans,false),
				Quatf(sample(retargeted,clip,c.rotation_input_accessor,c.rotation_output_accessor,t,n.rot.v,true)),
				sample(retargeted,clip,c.scale_input_accessor,c.scale_output_accessor,t,n.scale,false));
			world[i] = n.parent_index < 0 ? local : world[n.parent_index]*n.retarget_adjustment*local;
		}
		for(int i : driver.sorted_nodes) {
			const auto& n = driver.nodes[i];
			Matrix4f local = trs(n.trans,n.rot,n.scale);
			if(mapping[i] >= 0) {
				const int j=mapping[i];
				target_world[i] = world[j]*retargeted.nodes[j].inverse_bind_matrix*bind[i];
				if(n.parent_index < 0) local=target_world[i];
				else { Matrix4f inverse; check(target_world[n.parent_index].getInverseForAffine3Matrix(inverse), "Singular animated parent."); local=inverse*target_world[i]; }
			} else target_world[i] = n.parent_index < 0 ? local : target_world[n.parent_index]*local;
			Vec4f scale(local.getColumn(0).length(),local.getColumn(1).length(),local.getColumn(2).length(),0);
			check(local.upperLeftDeterminant()>0 && scale[0]>0 && scale[1]>0 && scale[2]>0, "Invalid animated bone scale.");
			Matrix4f rotation(local.getColumn(0)/scale[0],local.getColumn(1)/scale[1],local.getColumn(2)/scale[2],Vec4f(0,0,0,1));
			check(std::abs(dot(rotation.getColumn(0), rotation.getColumn(1)))<.0001f &&
				std::abs(dot(rotation.getColumn(0), rotation.getColumn(2)))<.0001f &&
				std::abs(dot(rotation.getColumn(1), rotation.getColumn(2)))<.0001f, "Sheared animation transform cannot be retargeted.");
			const Quatf q = Quatf::fromMatrix(rotation);
			check(std::abs(q.norm()-1)<.001f, "Sheared animation transform cannot be retargeted.");
			result->output_data[i*3].push_back(Vec4f(local.e[12],local.e[13],local.e[14],0));
			result->output_data[i*3+1].push_back(q.v);
			result->output_data[i*3+2].push_back(scale);
		}
	}
	// Do not retain thousands of duplicate scale/rest keys in the runtime cache.
	for(size_t i=0; i<driver.nodes.size(); ++i) {
		auto& c=out_clip->raw_per_anim_node_data[i];
		int* indices[]={&c.translation_input_accessor,&c.rotation_input_accessor,&c.scale_input_accessor};
		for(int v=0; v<3; ++v) {
			auto& values=result->output_data[i*3+v]; bool constant=true;
			for(size_t k=1; k<values.size(); ++k) if(values[k]!=values[0]) { constant=false; break; }
			if(constant) {
				if(result->keyframe_times.size()==1) { result->keyframe_times.resize(2); result->keyframe_times[1].times={0.f,duration}; }
				js::Vector<Vec4f, 16> compact(2, values[0]);
				values.swapWith(compact); *indices[v]=1;
			}
		}
	}
	result->animations.push_back(out_clip); result->build(); result->checkDataIsValid();
	return result;
}
