#pragma once

#include <graphics/AnimationData.h>
#include <algorithm>
#include <cmath>


namespace AvatarGrounding
{
static constexpr float kDefaultAvatarEyeHeightM = 1.67f;
static constexpr float kDefaultPlayerCapsuleRadiusM = 0.3f;
static constexpr float kDefaultPlayerCapsuleCylinderHeightM = 1.3f;
static constexpr float kDefaultPlayerSittingCylinderHeightM = 0.3f;
static constexpr float kHeadNodeToEyesOffsetM = 0.076f;
static constexpr float kDefaultToeBottomOffsetM = 0.0362269469f;
static constexpr float kRetargetedToeBottomOffsetM = 0.03f;


struct GroundingInfo
{
	float foot_bottom_height;
	float anchor_height_from_origin;
	float eye_height_above_ground;
};


struct PlayerControllerProfile
{
	float scale;
	float radius;
	float standing_cylinder_height;
	float sitting_cylinder_height;
};


inline float estimateEyeHeightFromHeadAndFoot(float head_height, float foot_height, float toe_bottom_offset_m)
{
	return (head_height + kHeadNodeToEyesOffsetM) - (foot_height - toe_bottom_offset_m);
}


inline float sanitiseAvatarEyeHeight(float eye_height)
{
	if(!std::isfinite(eye_height) || eye_height < 0.1f || eye_height > 5.0f)
		return kDefaultAvatarEyeHeightM;
	return eye_height;
}


inline PlayerControllerProfile computePlayerControllerProfile(float eye_height)
{
	const float valid_eye_height = sanitiseAvatarEyeHeight(eye_height);
	// Keep malformed or unusually extreme model metadata from creating an
	// unstable physics shape while still supporting child and oversized rigs.
	const float scale = std::max(0.25f, std::min(3.0f, valid_eye_height / kDefaultAvatarEyeHeightM));
	return PlayerControllerProfile{
		scale,
		kDefaultPlayerCapsuleRadiusM * scale,
		kDefaultPlayerCapsuleCylinderHeightM * scale,
		kDefaultPlayerSittingCylinderHeightM * scale
	};
}


inline bool tryGetNodeHeight(const AnimationData& animation_data, const char* node_name, bool use_retarget_adjustment, float& height_out)
{
	const int node_i = animation_data.getNodeIndex(node_name);
	if(node_i < 0)
		return false;

	const Vec4f node_pos = animation_data.getNodePositionModelSpace(node_i, use_retarget_adjustment);
	if(!std::isfinite(node_pos[1]))
		return false;

	height_out = node_pos[1];
	return true;
}


inline GroundingInfo computeGroundingInfo(const AnimationData& animation_data, bool use_retarget_adjustment, float toe_bottom_offset_m)
{
	GroundingInfo info = {};

	// Some VRM models have no *_Toe_End nodes. Using a zero fallback for those
	// models makes the avatar float when the model origin is above the soles.
	// Prefer toe tips, then fall back to both ankle/foot joints and use the
	// lowest valid side so asymmetrical rigs are grounded consistently.
	float lowest_foot_height = 0.0f;
	bool have_foot_height = false;
	for(const char* node_name : {"LeftToe_End", "RightToe_End", "LeftFoot", "RightFoot"})
	{
		float foot_height = 0.0f;
		if(tryGetNodeHeight(animation_data, node_name, use_retarget_adjustment, foot_height))
		{
			if(!have_foot_height || foot_height < lowest_foot_height)
				lowest_foot_height = foot_height;
			have_foot_height = true;
		}
	}
	info.foot_bottom_height = have_foot_height ? (lowest_foot_height - toe_bottom_offset_m) : 0.0f;

	float left_eye_height = 0.0f;
	float right_eye_height = 0.0f;
	float head_height = 0.0f;
	bool using_head_eye_fallback = false;
	const bool has_left_eye = tryGetNodeHeight(animation_data, "LeftEye", use_retarget_adjustment, left_eye_height);
	const bool has_right_eye = tryGetNodeHeight(animation_data, "RightEye", use_retarget_adjustment, right_eye_height);

	if(has_left_eye && has_right_eye)
		info.anchor_height_from_origin = 0.5f * (left_eye_height + right_eye_height);
	else if(has_left_eye)
		info.anchor_height_from_origin = left_eye_height;
	else if(has_right_eye)
		info.anchor_height_from_origin = right_eye_height;
	else if(tryGetNodeHeight(animation_data, "Head", use_retarget_adjustment, head_height))
	{
		info.anchor_height_from_origin = head_height + kHeadNodeToEyesOffsetM;
		using_head_eye_fallback = true;
	}
	else
		info.anchor_height_from_origin = info.foot_bottom_height + kDefaultAvatarEyeHeightM;

	info.eye_height_above_ground = (using_head_eye_fallback && have_foot_height) ?
		estimateEyeHeightFromHeadAndFoot(head_height, lowest_foot_height, toe_bottom_offset_m) :
		info.anchor_height_from_origin - info.foot_bottom_height;
	if(!(std::isfinite(info.eye_height_above_ground) && (info.eye_height_above_ground > 0.1f)))
	{
		info.eye_height_above_ground = kDefaultAvatarEyeHeightM;
		info.anchor_height_from_origin = info.foot_bottom_height + info.eye_height_above_ground;
	}

	return info;
}
}
