#pragma once

#include <graphics/AnimationData.h>

namespace VRMObjectAnimation
{
inline void commitPrepared(AnimationData& data, AnimationData& staged)
{
	// Commit without allocations: assigning AnimationData here could throw halfway
	// through and also lose the retarget flag. Keep its embedded reference count.
	// Keep this field list in sync with AnimationData::operator=.
	data.nodes.swap(staged.nodes);
	data.sorted_nodes.swap(staged.sorted_nodes);
	data.joint_nodes.swap(staged.joint_nodes);
	data.keyframe_times.swapWith(staged.keyframe_times);
	data.output_data.swapWith(staged.output_data);
	data.animations.swap(staged.animations);
	data.per_anim_node_data.swap(staged.per_anim_node_data);
	data.vrm_data.takeFrom(staged.vrm_data);
	data.retarget_adjustments_set = staged.retarget_adjustments_set;
}

// idle is the prepared Idle driver from AnimationManager. Ineligible meshes
// are untouched; retarget errors propagate without changing the destination.
// The caller owns object eligibility, main-thread scheduling and cache accounting.
inline bool addIdleIfMissing(AnimationData& data, const AnimationData& idle)
{
	if(data.vrm_data.isNull() || data.joint_nodes.empty() ||
		!data.animations.empty() || data.retarget_adjustments_set)
		return false;
	AnimationData staged;
	staged = data;
	staged.retarget_adjustments_set = data.retarget_adjustments_set;
	staged.loadAndRetargetAnim(idle);
	commitPrepared(data, staged);
	return true;
}
}
