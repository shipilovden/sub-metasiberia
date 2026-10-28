#include "AvatarGroundingUtils.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
bool near(float a, float b, float tolerance = 1.0e-4f)
{
	return std::fabs(a - b) <= tolerance;
}

bool expect(bool condition, const char* description)
{
	if(!condition)
		std::cerr << "FAIL: " << description << "\n";
	return condition;
}

}

int main()
{
	bool ok = true;

	// Both polyfork GLBs have Head at 1.0297496 m and Left/RightFoot at
	// 0.0609720 m, with no eye or toe-end nodes. The shared grounding helper's
	// documented fallback gives eye-to-sole = head + eye offset - foot + toe allowance.
	const float head_height = 1.0297496f;
	const float foot_height = 0.0609720f;
	const float eye_height = AvatarGrounding::estimateEyeHeightFromHeadAndFoot(
		head_height, foot_height, AvatarGrounding::kRetargetedToeBottomOffsetM);
	const float measured_eye_height = 1.0747786f;
	ok &= expect(near(eye_height, measured_eye_height), "polyfork GLB eye-to-sole measurement");

	const AvatarGrounding::PlayerControllerProfile small =
		AvatarGrounding::computePlayerControllerProfile(measured_eye_height);
	ok &= expect(near(small.scale, measured_eye_height / AvatarGrounding::kDefaultAvatarEyeHeightM), "small-avatar controller scale");
	ok &= expect(near(small.radius, AvatarGrounding::kDefaultPlayerCapsuleRadiusM * small.scale), "small-avatar capsule radius scaling");
	ok &= expect(near(small.standing_cylinder_height, AvatarGrounding::kDefaultPlayerCapsuleCylinderHeightM * small.scale), "small-avatar capsule height scaling");
	ok &= expect(small.radius > 0 && small.standing_cylinder_height > 0, "small-avatar collider remains valid");

	const AvatarGrounding::PlayerControllerProfile ordinary =
		AvatarGrounding::computePlayerControllerProfile(AvatarGrounding::kDefaultAvatarEyeHeightM);
	ok &= expect(near(ordinary.scale, 1.0f), "ordinary-avatar scale remains unchanged");
	ok &= expect(near(ordinary.radius, AvatarGrounding::kDefaultPlayerCapsuleRadiusM), "ordinary-avatar radius remains unchanged");
	ok &= expect(near(ordinary.standing_cylinder_height, AvatarGrounding::kDefaultPlayerCapsuleCylinderHeightM), "ordinary-avatar capsule height remains unchanged");

	const AvatarGrounding::PlayerControllerProfile tall = AvatarGrounding::computePlayerControllerProfile(4.5f);
	ok &= expect(near(tall.scale, 4.5f / AvatarGrounding::kDefaultAvatarEyeHeightM), "large-avatar scale is not truncated prematurely");
	ok &= expect(AvatarGrounding::sanitiseAvatarEyeHeight(0.05f) == AvatarGrounding::kDefaultAvatarEyeHeightM, "invalid model height falls back safely");
	ok &= expect(AvatarGrounding::sanitiseAvatarEyeHeight(std::numeric_limits<float>::quiet_NaN()) == AvatarGrounding::kDefaultAvatarEyeHeightM, "non-finite model height falls back safely");

	// Both systems place the avatar origin at eye level, so the visual soles and
	// resized controller bottom must coincide for the same world-space eye pose.
	const float model_foot_world = measured_eye_height - eye_height;
	const float controller_foot_world = measured_eye_height - small.scale * AvatarGrounding::kDefaultAvatarEyeHeightM;
	ok &= expect(near(model_foot_world, controller_foot_world), "visual feet and controller bottom remain aligned");

	if(ok)
		std::cout << "Avatar grounding profile checks passed.\n";
	return ok ? 0 : 1;
}
