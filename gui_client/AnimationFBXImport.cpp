#include "AnimationFBXImport.h"
#include "../third_party/ufbx/ufbx.h"
#include <utils/Exception.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {
void check(bool value, const char* message) { if(!value) throw glare::Exception(message); }
Vec4f vec(ufbx_vec3 v, float w) { return Vec4f(float(v.x), float(v.y), float(v.z), w); }
Quatf quat(ufbx_quat q) { return Quatf(float(q.x), float(q.y), float(q.z), float(q.w)); }
Matrix4f matrix(const ufbx_matrix& m)
{
	Matrix4f result;
	for(int c = 0; c < 4; ++c) result.setColumn(c, vec(m.cols[c], c == 3 ? 1.f : 0.f));
	return result;
}
}

Reference<AnimationData> readFBXAnimation(const QByteArray& bytes, const std::function<bool()>& cancelled)
{
	ufbx_load_opts opts = {};
	opts.ignore_geometry = true;
	opts.ignore_embedded = true;
	opts.skip_skin_vertices = true;
	opts.load_external_files = false;
	opts.node_depth_limit = 128;
	opts.temp_allocator.memory_limit = 128 * 1024 * 1024;
	opts.result_allocator.memory_limit = 128 * 1024 * 1024;
	opts.target_axes = ufbx_axes_right_handed_y_up;
	opts.target_unit_meters = 1.0;
	opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
	opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
	opts.file_format = UFBX_FILE_FORMAT_FBX;
	opts.progress_cb.fn = [](void* user, const ufbx_progress*) {
		const auto& stop = *static_cast<const std::function<bool()>*>(user);
		return stop && stop() ? UFBX_PROGRESS_CANCEL : UFBX_PROGRESS_CONTINUE;
	};
	opts.progress_cb.user = const_cast<std::function<bool()>*>(&cancelled);
	ufbx_error error = {};
	std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> scene(ufbx_load_memory(bytes.constData(), bytes.size(), &opts, &error), ufbx_free_scene);
	if(!scene)
	{
		if(cancelled && cancelled()) throw glare::Exception("Импорт отменён.");
		throw glare::Exception(std::string("Не удалось прочитать FBX: ") + std::string(error.description.data, error.description.length));
	}
	check(scene->nodes.count > 0 && scene->nodes.count <= 512, "FBX: допускается не более 512 узлов скелета.");
	check(scene->anim_stacks.count == 1, "FBX должен содержать одну анимацию. Экспортируйте один клип Mixamo.");
	check(scene->bones.count > 0, "В FBX нет скелета для анимации.");
	const ufbx_anim_stack* stack = scene->anim_stacks[0];
	const double duration = stack->time_end - stack->time_begin;
	check(std::isfinite(duration) && std::isfinite(stack->time_begin) && duration > 0 && duration <= 3600, "FBX: недопустимая длительность анимации.");
	const size_t intervals = size_t(std::ceil(duration * 60.0));
	const size_t frames = intervals + 1;
	check(frames <= 100000 && frames * (scene->nodes.count * 3 + 1) <= 2000000, "FBX слишком велик для покадрового импорта. Сократите клип.");
	Reference<AnimationData> data = new AnimationData();
	const size_t count = scene->nodes.count;
	data->nodes.resize(count);
	std::vector<ufbx_matrix> bind_world(count);
	for(const ufbx_node* node : scene->nodes) bind_world[node->typed_id] = node->node_to_world;
	// A skinned FBX can store its bind pose separately from the default node pose.
	for(const ufbx_skin_cluster* cluster : scene->skin_clusters)
		if(cluster->bone_node) bind_world[cluster->bone_node->typed_id] = cluster->bind_to_world;
	std::vector<int> order;
	std::function<void(const ufbx_node*)> visit = [&](const ufbx_node* node) {
		order.push_back(int(node->typed_id));
		for(const auto* child : node->children) visit(child);
	};
	visit(scene->root_node);
	check(order.size() == count, "FBX содержит несвязанные узлы скелета.");
	data->sorted_nodes = order;
	for(const ufbx_node* node : scene->nodes)
	{
		const size_t i = node->typed_id;
		auto& dest = data->nodes[i];
		dest.name.assign(node->name.data, node->name.length);
		dest.parent_index = node->parent ? int(node->parent->typed_id) : -1;
		ufbx_matrix local = bind_world[i];
		if(node->parent) {
			const auto parent_inverse = ufbx_matrix_invert(&bind_world[node->parent->typed_id]);
			local = ufbx_matrix_mul(&parent_inverse, &bind_world[i]);
		}
		const auto transform = ufbx_matrix_to_transform(&local);
		dest.trans = vec(transform.translation, 0);
		dest.rot = quat(transform.rotation);
		dest.scale = vec(transform.scale, 0);
		dest.inverse_bind_matrix = matrix(ufbx_matrix_invert(&bind_world[i]));
		if(node->bone) data->joint_nodes.push_back(int(i));
	}
	Reference<AnimationDatum> anim = new AnimationDatum();
	anim->name.assign(stack->name.data, stack->name.length);
	anim->raw_per_anim_node_data.resize(count);
	data->keyframe_times.resize(1);
	data->output_data.resize(count * 3);
	for(size_t i = 0; i < count; ++i) {
		auto& channel = anim->raw_per_anim_node_data[i];
		channel.translation_input_accessor = channel.rotation_input_accessor = channel.scale_input_accessor = 0;
		channel.translation_output_accessor = int(i * 3);
		channel.rotation_output_accessor = int(i * 3 + 1);
		channel.scale_output_accessor = int(i * 3 + 2);
		for(size_t c = 0; c < 3; ++c) data->output_data[i * 3 + c].reserve(frames);
	}
	for(size_t f = 0; f < frames; ++f)
	{
		check(!cancelled || !cancelled(), "Импорт отменён.");
		const double t = duration * double(f) / double(intervals);
		data->keyframe_times[0].times.push_back(float(t));
		for(const ufbx_node* node : scene->nodes) {
			const size_t i = node->typed_id;
			const auto transform = ufbx_evaluate_transform(stack->anim, node, stack->time_begin + t);
			data->output_data[i * 3].push_back(vec(transform.translation, 0));
			data->output_data[i * 3 + 1].push_back(quat(transform.rotation).v);
			data->output_data[i * 3 + 2].push_back(vec(transform.scale, 0));
		}
	}
	// Constant channels need only endpoints; retain clip duration even for a static pose.
	for(size_t i = 0; i < count; ++i) {
		auto& channel = anim->raw_per_anim_node_data[i];
		int* inputs[] = {&channel.translation_input_accessor, &channel.rotation_input_accessor, &channel.scale_input_accessor};
		for(size_t c = 0; c < 3; ++c) {
			auto& values = data->output_data[i * 3 + c];
			bool constant = true;
			for(size_t k = 1; k < values.size(); ++k) if(values[k] != values[0]) { constant = false; break; }
			if(constant) {
				if(data->keyframe_times.size() == 1) { data->keyframe_times.resize(2); data->keyframe_times[1].times = {0.f, float(duration)}; }
				values.resize(2); *inputs[c] = 1;
			}
		}
	}
	data->animations.push_back(anim);
	data->build();
	return data;
}
