#include "../../gui_client/VRMObjectAnimation.h"
#include <utils/BufferOutStream.h>
#include <utils/BufferViewInStream.h>
#include <utils/FileInStream.h>
#include <utils/Exception.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message)
{
	if(!value) throw std::runtime_error(message);
}

void writeSamples(BufferOutStream& out, const js::Vector<KeyFrameTimeInfo>& times,
	const js::Vector<js::Vector<Vec4f, 16>>& values)
{
	out.writeUInt32((uint32)times.size());
	for(const auto& track : times)
	{
		out.writeUInt32((uint32)track.times.size());
		for(float time : track.times) out.writeData(&time, sizeof(time));
	}
	out.writeUInt32((uint32)values.size());
	for(const auto& track : values)
	{
		out.writeUInt32((uint32)track.size());
		for(const auto& value : track) out.writeData(value.x, sizeof(float) * 4);
	}
}

// Includes runtime retarget/clip data that AnimationData::writeToStream omits.
std::vector<uint8> snapshot(const AnimationData& data)
{
	BufferOutStream out;
	out.writeUInt32(data.retarget_adjustments_set ? 1 : 0);
	out.writeUInt32((uint32)data.nodes.size());
	for(const auto& node : data.nodes)
	{
		node.writeToStream(out);
		out.writeData(node.retarget_adjustment.e, sizeof(float) * 16);
	}
	for(const auto* indices : {&data.sorted_nodes, &data.joint_nodes})
	{
		out.writeUInt32((uint32)indices->size());
		for(int index : *indices) out.writeInt32(index);
	}
	writeSamples(out, data.keyframe_times, data.output_data);
	out.writeUInt32((uint32)data.animations.size());
	for(const auto& clip : data.animations)
	{
		clip->writeToStream(out);
		out.writeData(&clip->anim_len, sizeof(clip->anim_len));
		out.writeUInt32((uint32)clip->used_input_accessor_indices.size());
		for(int index : clip->used_input_accessor_indices) out.writeInt32(index);
		writeSamples(out, clip->m_keyframe_times, clip->m_output_data);
	}
	out.writeUInt32((uint32)data.per_anim_node_data.size());
	for(const auto& channels : data.per_anim_node_data)
	{
		out.writeUInt32((uint32)channels.size());
		for(const auto& channel : channels) channel.writeToStream(out);
	}
	out.writeUInt32(data.vrm_data.nonNull() ? 1 : 0);
	if(data.vrm_data.nonNull())
	{
		out.writeUInt32((uint32)data.vrm_data->human_bones.size());
		for(const auto& bone : data.vrm_data->human_bones)
		{
			out.writeStringLengthFirst(bone.first);
			out.writeInt32(bone.second.node_index);
		}
	}
	return std::vector<uint8>(out.buf.begin(), out.buf.end());
}

void makeVRM(AnimationData& target, const AnimationData& idle)
{
	target.nodes = idle.nodes;
	target.sorted_nodes = idle.sorted_nodes;
	target.joint_nodes = idle.joint_nodes;
	target.vrm_data = new GLTFVRMExtension();
	// These bones deliberately cannot match canonical names without VRM metadata.
	const char* canonical[] = {"Hips", "Head", "LeftHand"};
	const char* vrm[] = {"hips", "head", "leftHand"};
	for(int i = 0; i < 3; ++i)
	{
		const int index = target.getNodeIndex(canonical[i]);
		check(index >= 0, "Idle fixture is missing a required bone");
		target.vrm_data->human_bones[vrm[i]] = VRMBoneInfo{index};
		target.nodes[index].name = std::string("VRMFixture_") + vrm[i];
	}
	target.checkDataIsValid();
}

void checkSkipped(AnimationData& target, const AnimationData& idle, const char* message)
{
	const auto before = snapshot(target);
	const auto* nodes = target.nodes.data();
	const auto* metadata = target.vrm_data.ptr();
	const size_t memory = target.getTotalMemUsage();
	check(!VRMObjectAnimation::addIdleIfMissing(target, idle), message);
	check(snapshot(target) == before && target.nodes.data() == nodes &&
		target.vrm_data.ptr() == metadata && target.getTotalMemUsage() == memory,
		"skipped mesh was mutated");
}
}

void testVRMObjectAnimation(const std::string& idle_path)
{
	FileInStream file(idle_path);
	char magic[4]; file.readData(magic, 4);
	check(std::string(magic, 4) == "SUBA", "invalid Idle fixture magic");
	AnimationData idle;
	idle.readFromStream(file);
	check(idle.animations.size() == 1 && idle.animations[0]->name == "Idle", "invalid Idle fixture");
	idle.prepareForMultipleUse(); // Same driver ownership as AnimationManager.
	const auto driver_before = snapshot(idle);

	AnimationData no_metadata, no_joints, authored, already_retargeted;
	for(auto* data : {&no_metadata, &no_joints, &authored, &already_retargeted}) makeVRM(*data, idle);
	no_metadata.vrm_data = nullptr;
	no_joints.joint_nodes.clear();
	authored.animations.push_back(new AnimationDatum());
	authored.animations[0]->name = "Authored";
	authored.animations[0]->anim_len = 1.f;
	authored.animations[0]->raw_per_anim_node_data.resize(authored.nodes.size());
	for(auto& channel : authored.animations[0]->raw_per_anim_node_data) channel.init();
	authored.per_anim_node_data.push_back(authored.animations[0]->raw_per_anim_node_data);
	already_retargeted.retarget_adjustments_set = true;
	checkSkipped(no_metadata, idle, "non-VRM mesh accepted");
	checkSkipped(no_joints, idle, "unskinned VRM accepted");
	checkSkipped(authored, idle, "authored animation replaced");
	checkSkipped(already_retargeted, idle, "retarget flag ignored");

	AnimationData source, target;
	makeVRM(source, idle);
	const auto source_before = snapshot(source);
	// Exercise the animation payload serialized inside .bmesh, without user assets.
	BufferOutStream encoded;
	source.writeToStream(encoded);
	BufferViewInStream input(ArrayRef<uint8>(encoded.buf.data(), encoded.buf.size()));
	target.readFromStream(input);
	check(snapshot(target) == source_before, "VRM metadata/rest skeleton roundtrip changed");
	const int old_hips = target.vrm_data->human_bones.at("hips").node_index;
	const auto old_joints = target.joint_nodes;
	target.setAsNotIndependentlyHeapAllocated(); // Same ownership as embedded render animation data.
	const auto refcount_before = target.getRefCount();
	check(VRMObjectAnimation::addIdleIfMissing(target, idle), "clipless VRM did not get Idle");
	check(target.retarget_adjustments_set, "retarget flag lost during commit");
	check(target.getRefCount() == refcount_before, "embedded ownership changed during commit");
	check(target.animations.size() == 1 && target.getAnimationIndex("Idle") == 0, "Idle is not default index zero");
	check(target.animations[0] == idle.animations[0], "prepared Idle clip was unnecessarily copied");
	check(target.vrm_data == idle.vrm_data, "stale source VRM node indices retained");
	check(target.joint_nodes.size() == old_joints.size(), "skin joint count changed");
	bool checked_hips = false;
	for(size_t j = 0; j < old_joints.size(); ++j)
		if(old_joints[j] == old_hips)
		{
			check(target.nodes[target.joint_nodes[j]].name == "Hips", "VRM hips metadata was not used");
			checked_hips = true;
		}
	check(checked_hips, "fixture has no hips joint");
	target.checkDataIsValid();
	target.checkPerAnimNodeDataIsValid();
	for(const auto& node : target.nodes)
		for(int i = 0; i < 16; ++i)
			check(std::isfinite(node.inverse_bind_matrix.e[i]) && std::isfinite(node.retarget_adjustment.e[i]), "nonfinite retarget matrix");
	checkSkipped(target, idle, "second call installed Idle again");
	check(snapshot(source) == source_before, "serialized source was mutated");

	// Object -> avatar: preserve the retargeted skeleton and Idle pose while
	// extending a separate animation container with locomotion. No GL is needed.
	const auto world_idle_before = snapshot(target);
	AnimationData avatar;
	avatar.setAsNotIndependentlyHeapAllocated();
	const auto avatar_refcount = avatar.getRefCount();
	avatar = target;
	avatar.retarget_adjustments_set = target.retarget_adjustments_set;
	check(avatar.retarget_adjustments_set && avatar.getRefCount() == avatar_refcount,
		"avatar copy lost retarget flag or copied source ownership");
	check(snapshot(avatar) == world_idle_before, "avatar copy changed Idle pose data");

	const size_t slash = idle_path.find_last_of("/\\");
	const std::string walking_path = idle_path.substr(0, slash + 1) + "Walking.subanim";
	FileInStream walking_file(walking_path);
	walking_file.readData(magic, 4);
	check(std::string(magic, 4) == "SUBA", "invalid Walking fixture magic");
	AnimationData walking;
	walking.readFromStream(walking_file);
	check(walking.animations.size() == 1 && walking.animations[0]->name == "Walking", "invalid Walking fixture");
	walking.prepareForMultipleUse();
	for(int pass = 0; pass < 2; ++pass)
	{
		AnimationData staged;
		staged = avatar;
		staged.retarget_adjustments_set = avatar.retarget_adjustments_set;
		if(!staged.retarget_adjustments_set) staged.loadAndRetargetAnim(idle);
		if(staged.getAnimationIndex("Walking") < 0) staged.appendAnimationData(walking);
		VRMObjectAnimation::commitPrepared(avatar, staged);
	}
	check(avatar.retarget_adjustments_set && avatar.getRefCount() == avatar_refcount,
		"staged avatar commit lost flag or embedded ownership");
	check(avatar.animations.size() == 2 && avatar.getAnimationIndex("Idle") == 0 &&
		avatar.getAnimationIndex("Walking") == 1, "avatar locomotion duplicated or shifted Idle");
	avatar.checkDataIsValid();
	avatar.checkPerAnimNodeDataIsValid();
	check(snapshot(target) == world_idle_before, "avatar preparation mutated world Idle source");
	// Exact equality of skeleton, bind/retarget matrices, Idle samples and mapped
	// channels proves identical Idle skinning at every time, not just frame zero.
	AnimationData avatar_idle;
	avatar_idle = avatar;
	avatar_idle.retarget_adjustments_set = avatar.retarget_adjustments_set;
	avatar_idle.animations.resize(1);
	avatar_idle.per_anim_node_data.resize(1);
	check(snapshot(avatar_idle) == world_idle_before, "avatar locomotion preparation introduced an Idle pose jump");

	// Invalid metadata throws inside retargeting AFTER it replaces staged arrays.
	AnimationData invalid;
	makeVRM(invalid, idle);
	invalid.vrm_data->human_bones["hips"].node_index = (int)invalid.nodes.size();
	const auto invalid_before = snapshot(invalid);
	const auto* invalid_nodes = invalid.nodes.data();
	const auto* invalid_metadata = invalid.vrm_data.ptr();
	const size_t invalid_memory = invalid.getTotalMemUsage();
	bool threw = false;
	try { VRMObjectAnimation::addIdleIfMissing(invalid, idle); }
	catch(const glare::Exception&) { threw = true; }
	check(threw, "invalid VRM metadata did not throw");
	check(snapshot(invalid) == invalid_before && invalid.nodes.data() == invalid_nodes &&
		invalid.vrm_data.ptr() == invalid_metadata && invalid.getTotalMemUsage() == invalid_memory,
		"failed retarget mutated original data");
	check(snapshot(idle) == driver_before, "shared Idle driver was mutated");
	std::cout << "PASS: VRM Idle guards, metadata roundtrip, default index, flag/ownership commit, idempotence, failure preservation\n";
	std::cout << "PASS: world Idle source unchanged; avatar copy flag/ownership and locomotion preserve Idle pose without a jump\n";
}
