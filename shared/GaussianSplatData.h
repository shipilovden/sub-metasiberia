/*=====================================================================
GaussianSplatData.h
-------------------
Canonical CPU representation and bounded decoders for Gaussian splats.
=====================================================================*/
#pragma once

#include "GaussianSplatAsset.h"

#include <utils/ArrayRef.h>
#include <utils/Reference.h>
#include <utils/ThreadSafeRefCounted.h>
#include <physics/jscol_aabbox.h>

#include <string>
#include <vector>


// Four RGBA32F texels.  This deliberately forms a stable CPU/GPU boundary:
//   texel 0: position.xyz, opacity
//   texel 1: scale.xyz, reserved
//   texel 2: quaternion.xyzw
//   texel 3: display-space SH-DC RGB, reserved
//
// colour_* is intentionally not clamped or converted to linear space here.
// Directional SH terms must be added first; the renderer clamps and converts
// the reconstructed display colour to linear space afterwards.
struct GaussianSplat
{
	float position_x, position_y, position_z, opacity;
	float scale_x, scale_y, scale_z, reserved_0;
	float rotation_x, rotation_y, rotation_z, rotation_w;
	float colour_r, colour_g, colour_b, reserved_1;
};


class GaussianSplatData : public ThreadSafeRefCounted
{
public:
	GaussianSplatData();

	std::vector<GaussianSplat> splats;
	// Source SH degree (0 through 3).  spherical_harmonics stores the f_rest
	// values for each splat consecutively, channel-major within each splat:
	// R coefficients, then G coefficients, then B coefficients.
	int sh_degree;
	std::vector<float> spherical_harmonics;
	js::AABBox aabb_os;
	GaussianSplatAsset::Format source_format;
};

typedef Reference<GaussianSplatData> GaussianSplatDataRef;


// Per-object render tuning.  Stored in WorldObject::content for Gaussian
// objects, so existing servers and older clients can preserve it without
// needing a new network object type.
struct GaussianSplatRenderSettings
{
	GaussianSplatRenderSettings();

	float opacity_multiplier;
	float brightness;
	float radius_multiplier;
	float saturation;
	float contrast;
	float alpha_cutoff;
	float minimum_source_opacity;
	// -1 preserves the source degree; 0 through 3 cap it explicitly.
	int sh_degree_override;

	bool isDefault() const;
	std::string cacheKeySuffix() const;

	static bool isSettingsContent(const std::string& content);
	static GaussianSplatRenderSettings fromContent(const std::string& content);
	static std::string serialiseToContent(const GaussianSplatRenderSettings& settings);
	static GaussianSplatDataRef applyToData(const GaussianSplatData& source, const GaussianSplatRenderSettings& settings);
};


namespace GaussianSplatDecoder
{
// Decode a supported Gaussian asset.  The first native milestone supports
// binary_little_endian 3DGS PLY and the common 32-byte .splat stream.
// Recognised future containers fail with an explicit, extensible diagnostic.
GaussianSplatDataRef decode(const string_view source_name, ArrayRef<uint8> data);

void test();
}
