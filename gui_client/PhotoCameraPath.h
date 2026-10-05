#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <stdexcept>

// Native photo tooling; never moves the avatar or edits world state.
namespace PhotoCameraPath {
struct Keyframe {
	std::array<double, 3> position{}, angles{}; // heading, pitch, roll in radians
	double focus = 3, lens_mm = 25;
};
inline double shortestAngle(double from, double to) {
	return std::remainder(to - from, 6.28318530717958647692);
}
inline Keyframe sample(const std::vector<Keyframe>& keys, double fraction) {
	if(keys.empty()) throw std::runtime_error("Camera path has no keyframes");
	if(keys.size() == 1 || fraction <= 0) return keys.front();
	if(fraction >= 1) return keys.back();
	const double scaled = fraction * (keys.size() - 1);
	const size_t i = std::min(keys.size() - 2, static_cast<size_t>(scaled));
	const double t = scaled - i;
	// Smooth arrivals/departures at each keyframe, with no spatial overshoot.
	const double u = t*t*(3 - 2*t);
	const auto& a = keys[i]; const auto& b = keys[i+1];
	Keyframe result;
	for(size_t axis=0; axis<3; ++axis) {
		result.position[axis] = a.position[axis] + (b.position[axis]-a.position[axis])*u;
		result.angles[axis] = a.angles[axis] + shortestAngle(a.angles[axis], b.angles[axis])*u;
	}
	result.focus = a.focus + (b.focus-a.focus)*u;
	result.lens_mm = a.lens_mm + (b.lens_mm-a.lens_mm)*u;
	return result;
}
}
