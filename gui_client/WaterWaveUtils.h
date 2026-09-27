#pragma once

#include <algorithm>
#include <cmath>

// Keep this surface in sync with water_surface.glsl.  Particle
// impacts must use the rendered crest, not a separate free-running timer.
namespace WaterWaveUtils
{
struct Sample
{
	float height;
	float vertical_speed;
};

inline float smooth(float lo, float hi, float x)
{
	const float u = std::clamp((x-lo)/(hi-lo), 0.f, 1.f);
	return u*u*(3.f-2.f*u);
}

inline float swash(float phase)
{
	const float cycles = -phase / 6.28318530718f;
	const float c = cycles - std::floor(cycles);
	return -0.25f + 1.25f * smooth(0.f, 0.24f, c) * (1.f-smooth(0.24f, 1.f, c));
}

inline Sample sample(float x, float y, double time, float amplitude, float wavelength,
	float speed, float direction, float spread, float secondary)
{
	const float a = std::clamp(amplitude, 0.f, 4.f) * 0.35f;
	const float k = 6.28318530718f / std::max(4.f, wavelength);
	const float omega = std::sqrt(9.8f * k) * std::max(0.f, speed);
	const float spread_angle = std::clamp(spread, 0.f, 1.4f);
	const float p = float((x * std::cos(direction) + y * std::sin(direction)) * k - time * omega);
	const float k2 = k * 1.73f;
	const float omega2 = std::sqrt(9.8f * k2) * std::max(0.f, speed) * 1.21f;
	const float p2 = float((x * std::cos(direction + spread_angle) + y * std::sin(direction + spread_angle)) * k2 - time * omega2 + 1.7);
	const float a2 = a * std::clamp(secondary, 0.f, 1.f) * 0.45f;
	return {a * std::sin(p) + a2 * std::sin(p2), -a * omega * std::cos(p) - a2 * omega2 * std::cos(p2)};
}

inline Sample sampleCoastal(float x, float y, double time, float amplitude, float wavelength,
	float speed, float direction, float spread, float secondary, float ground_relative_z, float gx, float gy)
{
	const Sample sea = sample(x, y, time, amplitude, wavelength, speed, direction, spread, secondary);
	const float a = std::clamp(amplitude, 0.f, 4.f) * 0.35f;
	const float slope = std::sqrt(gx*gx+gy*gy);
	const float shore = smooth(0.008f, 0.035f, slope) *
		(1.f-smooth(std::max(0.2f,a*1.5f), std::max(0.8f,a*6.f), std::max(0.f,-ground_relative_z)));
	const float distance = std::clamp(ground_relative_z/std::max(0.008f,slope), -60.f, 60.f);
	const float k = 6.28318530718f/std::max(4.f,wavelength);
	const float omega = std::sqrt(9.8f*k)*std::max(0.f,speed);
	const float phase = float(((x-gx/std::max(0.008f,slope)*distance)*std::cos(direction) +
		(y-gy/std::max(0.008f,slope)*distance)*std::sin(direction))*k-time*omega);
	const float c0 = -phase/6.28318530718f;
	const float c = c0-std::floor(c0);
	const float u = c < 0.24f ? c/0.24f : (c-0.24f)/0.76f;
	const float derivative = 1.25f*6.f*u*(1.f-u)*(c < 0.24f ? 1.f/0.24f : -1.f/0.76f)*omega/6.28318530718f;
	return {sea.height*(1.f-shore)+a*swash(phase)*shore, sea.vertical_speed*(1.f-shore)+a*derivative*shore};
}
}
