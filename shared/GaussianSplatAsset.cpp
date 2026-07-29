/*=====================================================================
GaussianSplatAsset.cpp
=====================================================================*/
#include "GaussianSplatAsset.h"

#include "../utils/StringUtils.h"
#if BUILD_TESTS
#include "../utils/ConPrint.h"
#include "../utils/TestUtils.h"
#endif

namespace GaussianSplatAsset
{
static string_view pathWithoutQueryOrFragment(string_view path)
{
	const size_t suffix_pos = path.find_first_of("?#");
	return suffix_pos == string_view::npos ? path : path.substr(0, suffix_pos);
}


Format detectFormat(string_view path)
{
	const string_view extension_path = pathWithoutQueryOrFragment(path);
	const std::string lowercase_path = toLowerCase(
		std::string(extension_path.data(), extension_path.size())
	);
	const size_t basename_pos = lowercase_path.find_last_of("/\\");
	const std::string basename = basename_pos == std::string::npos ?
		lowercase_path : lowercase_path.substr(basename_pos + 1);

	// SplatTransform's unbundled SOG entry points use these exact filenames.
	// Do not classify arbitrary JSON files as Gaussian resources.
	if(basename == "meta.json" || basename == "lod-meta.json")
		return Format::SOG;

	// Check the composite extension first: it is a distinct container despite
	// also ending in ".ply".
	if(hasExtension(extension_path, "compressed.ply")) return Format::CompressedPly;
	if(hasExtension(extension_path, "ply")) return Format::Ply;
	if(hasExtension(extension_path, "splat")) return Format::Splat;
	if(hasExtension(extension_path, "ksplat")) return Format::KSplat;
	if(hasExtension(extension_path, "spz")) return Format::SPZ;
	if(hasExtension(extension_path, "sog")) return Format::SOG;
	if(hasExtension(extension_path, "lcc2")) return Format::LCC2;
	if(hasExtension(extension_path, "lcc")) return Format::LCC;
	return Format::Unknown;
}

bool hasSupportedExtension(string_view path)
{
	return detectFormat(path) != Format::Unknown;
}

bool validatePlyHeader(const std::string& header, std::string& error_out)
{
	error_out.clear();

	static const size_t MAX_PLY_HEADER_SIZE = 1024 * 1024;
	if(header.empty() || header.size() > MAX_PLY_HEADER_SIZE)
	{
		error_out = "3DGS PLY header is empty or exceeds the 1 MiB limit.";
		return false;
	}

	bool first_line = true;
	bool valid_format = false;
	bool have_x = false;
	bool have_y = false;
	bool have_z = false;
	bool have_opacity = false;
	bool have_scale_0 = false;
	bool have_rot_0 = false;
	bool have_end_header = false;

	size_t line_begin = 0;
	while(line_begin < header.size())
	{
		const size_t newline_pos = header.find('\n', line_begin);
		const size_t line_end = newline_pos == std::string::npos ? header.size() : newline_pos;
		size_t content_end = line_end;
		if(content_end > line_begin && header[content_end - 1] == '\r')
			--content_end;

		const string_view line(header.data() + line_begin, content_end - line_begin);

		if(first_line)
		{
			if(line != "ply")
			{
				error_out = "3DGS PLY must start with an ASCII 'ply' header.";
				return false;
			}
			first_line = false;
		}
		else if(line == "format binary_little_endian 1.0" || line == "format ascii 1.0")
			valid_format = true;
		else if(line == "property float x")
			have_x = true;
		else if(line == "property float y")
			have_y = true;
		else if(line == "property float z")
			have_z = true;
		else if(line == "property float opacity")
			have_opacity = true;
		else if(line == "property float scale_0")
			have_scale_0 = true;
		else if(line == "property float rot_0")
			have_rot_0 = true;
		else if(line == "end_header")
		{
			have_end_header = true;
			break;
		}

		if(newline_pos == std::string::npos)
			break;
		line_begin = newline_pos + 1;
	}

	if(!valid_format)
	{
		error_out = "Unsupported PLY format; expected ASCII or binary_little_endian 1.0.";
		return false;
	}

	if(!have_end_header)
	{
		error_out = "PLY header is incomplete.";
		return false;
	}

	// The standard INRIA 3DGS export contains these fields. Unknown
	// properties, SH degree and property ordering remain unrestricted.
	if(!have_x)         { error_out = "PLY is not a 3DGS asset; missing field 'property float x'."; return false; }
	if(!have_y)         { error_out = "PLY is not a 3DGS asset; missing field 'property float y'."; return false; }
	if(!have_z)         { error_out = "PLY is not a 3DGS asset; missing field 'property float z'."; return false; }
	if(!have_opacity)   { error_out = "PLY is not a 3DGS asset; missing field 'property float opacity'."; return false; }
	if(!have_scale_0)   { error_out = "PLY is not a 3DGS asset; missing field 'property float scale_0'."; return false; }
	if(!have_rot_0)     { error_out = "PLY is not a 3DGS asset; missing field 'property float rot_0'."; return false; }

	return true;
}


#if BUILD_TESTS


void test()
{
	conPrint("GaussianSplatAsset::test()");

	testAssert(detectFormat("scene.ply") == Format::Ply);
	testAssert(detectFormat("scene.compressed.ply") == Format::CompressedPly);
	testAssert(detectFormat("SCENE.COMPRESSED.PLY") == Format::CompressedPly);
	testAssert(detectFormat("https://example.test/scene.compressed.ply?token=1#asset") == Format::CompressedPly);
	testAssert(detectFormat("scene.splat") == Format::Splat);
	testAssert(detectFormat("scene.ksplat") == Format::KSplat);
	testAssert(detectFormat("scene.spz") == Format::SPZ);
	testAssert(detectFormat("scene.sog") == Format::SOG);
	testAssert(detectFormat("meta.json") == Format::SOG);
	testAssert(detectFormat("assets/META.JSON?token=1") == Format::SOG);
	testAssert(detectFormat("assets/lod-meta.json#scene") == Format::SOG);
	testAssert(detectFormat("other.json") == Format::Unknown);
	testAssert(detectFormat("meta.json.backup") == Format::Unknown);
	testAssert(detectFormat("scene.lcc") == Format::LCC);
	testAssert(detectFormat("scene.lcc2") == Format::LCC2);
	testAssert(detectFormat("scene.ply.txt") == Format::Unknown);
	testAssert(detectFormat("scene") == Format::Unknown);

	testAssert(hasSupportedExtension("scene.ply"));
	testAssert(hasSupportedExtension("scene.compressed.ply"));
	testAssert(!hasSupportedExtension("scene.obj"));
	testAssert(!hasSupportedExtension(""));

	const std::string valid_lf_header =
		"ply\n"
		"format binary_little_endian 1.0\n"
		"element vertex 1\n"
		"property float x\n"
		"property float y\n"
		"property float z\n"
		"property float opacity\n"
		"property float scale_0\n"
		"property float rot_0\n"
		"end_header\n";

	const std::string valid_crlf_header =
		"ply\r\n"
		"format ascii 1.0\r\n"
		"property float rot_0\r\n"
		"property float scale_0\r\n"
		"property float opacity\r\n"
		"property float z\r\n"
		"property float y\r\n"
		"property float x\r\n"
		"end_header\r\n";

	std::string error;
	testAssert(validatePlyHeader(valid_lf_header, error));
	testAssert(error.empty());
	testAssert(validatePlyHeader(valid_crlf_header, error));
	testAssert(error.empty());

	testAssert(!validatePlyHeader("not-ply\n", error));
	testAssert(!error.empty());

	std::string big_endian_header = valid_lf_header;
	const size_t format_pos = big_endian_header.find("binary_little_endian");
	big_endian_header.replace(format_pos, std::string("binary_little_endian").size(), "binary_big_endian");
	testAssert(!validatePlyHeader(big_endian_header, error));

	std::string missing_opacity_header = valid_lf_header;
	const size_t opacity_pos = missing_opacity_header.find("property float opacity\n");
	missing_opacity_header.erase(opacity_pos, std::string("property float opacity\n").size());
	testAssert(!validatePlyHeader(missing_opacity_header, error));

	std::string comment_only_opacity_header = missing_opacity_header;
	const size_t end_header_pos = comment_only_opacity_header.find("end_header");
	comment_only_opacity_header.insert(end_header_pos, "comment property float opacity\n");
	testAssert(!validatePlyHeader(comment_only_opacity_header, error));

	std::string incomplete_header = valid_lf_header;
	incomplete_header.erase(incomplete_header.find("end_header"));
	testAssert(!validatePlyHeader(incomplete_header, error));

	conPrint("GaussianSplatAsset::test() done");
}


#endif // BUILD_TESTS
}
