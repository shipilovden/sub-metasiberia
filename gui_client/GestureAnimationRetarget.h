#pragma once
#include <graphics/AnimationData.h>
#include <functional>

// appendAnimationData() maps channel names, NOT coordinate frames. Convert a
// differently exported clip to the Idle driver skeleton once, before caching.
// Returns null when already compatible; never mutates either argument.
Reference<AnimationData> normaliseGestureAnimation(const AnimationData& source, const AnimationData& driver,
	const std::function<bool()>& cancelled = {});
