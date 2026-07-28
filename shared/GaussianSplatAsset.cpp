/*=====================================================================
GaussianSplatAsset.cpp
=====================================================================*/
#include "GaussianSplatAsset.h"

#include "../utils/StringUtils.h"

namespace GaussianSplatAsset
{
Format detectFormat(string_view path)
{
	if(hasExtension(path, "compressed.ply")) return Format::CompressedPly;
	if(hasExtension(path, "ply")) return Format::Ply;
	if(hasExtension(path, "splat")) return Format::Splat;
	if(hasExtension(path, "ksplat")) return Format::KSplat;
	if(hasExtension(path, "spz")) return Format::SPZ;
	if(hasExtension(path, "sog")) return Format::SOG;
	if(hasExtension(path, "lcc2")) return Format::LCC2;
	if(hasExtension(path, "lcc")) return Format::LCC;
	return Format::Unknown;
}

bool hasSupportedExtension(string_view path)
{
	return hasExtension(path, "ply") || hasExtension(path, "splat") || hasExtension(path, "ksplat") ||
		hasExtension(path, "spz") || hasExtension(path, "sog") || hasExtension(path, "lcc") ||
		hasExtension(path, "lcc2") || hasExtension(path, "compressed.ply");
}

bool validatePlyHeader(const std::string& header, std::string& error_out)
{
	if(header.size() < 16 || header.compare(0, 4, "ply\n") != 0)
	{
		error_out = "3DGS PLY must start with an ASCII 'ply' header.";
		return false;
	}

	if(header.find("format binary_little_endian 1.0") == std::string::npos &&
		header.find("format ascii 1.0") == std::string::npos)
	{
		error_out = "Unsupported PLY format; expected ASCII or binary_little_endian 1.0.";
		return false;
	}

	// The standard 3DGS export contains these fields. We intentionally do not
	// require a specific SH degree or property ordering.
	const char* required[] = { "property float x", "property float y", "property float z", "opacity", "scale_0", "rot_0" };
	for(const char* field : required)
	{
		if(header.find(field) == std::string::npos)
		{
			error_out = std::string("PLY is not a 3DGS asset; missing field '") + field + "'.";
			return false;
		}
	}

	if(header.find("end_header") == std::string::npos)
	{
		error_out = "PLY header is incomplete.";
		return false;
	}
	return true;
}
}
