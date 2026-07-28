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

#include <vector>


// Four RGBA32F texels.  This deliberately forms a stable CPU/GPU boundary:
//   texel 0: position.xyz, opacity
//   texel 1: scale.xyz, reserved
//   texel 2: quaternion.xyzw
//   texel 3: linear RGB, reserved
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
	js::AABBox aabb_os;
	GaussianSplatAsset::Format source_format;
};

typedef Reference<GaussianSplatData> GaussianSplatDataRef;


namespace GaussianSplatDecoder
{
// Decode a supported Gaussian asset.  The first native milestone supports
// binary_little_endian 3DGS PLY and the common 32-byte .splat stream.
// Recognised future containers fail with an explicit, extensible diagnostic.
GaussianSplatDataRef decode(const string_view source_name, ArrayRef<uint8> data);

void test();
}
