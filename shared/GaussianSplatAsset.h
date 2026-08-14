/*=====================================================================
GaussianSplatAsset.h
--------------------
Bounded validation for uploaded Gaussian Splat resources.
=====================================================================*/
#pragma once

#include "../utils/StringUtils.h"

#include <string>

namespace GaussianSplatAsset
{
enum class Format
{
	Unknown,
	Ply,
	CompressedPly,
	Splat,
	KSplat,
	SPZ,
	SOG,
	LCC,
	LCC2
};

Format detectFormat(string_view path);

// Supported container names, including the exact `meta.json` and
// `lod-meta.json` entry points used by unbundled/streamed SOG. Rendering is
// deliberately kept in the client; this helper also protects the shared
// upload/resource boundary.
bool hasSupportedExtension(string_view path);

// Validate the beginning of an INRIA-style 3DGS PLY header. The complete
// payload is not loaded into memory, and unknown PLY properties are allowed.
// Returns false with a bounded diagnostic for ordinary point clouds/meshes.
bool validatePlyHeader(const std::string& header, std::string& error_out);

void test();
}
