/*=====================================================================
GaussianSplatData.cpp
=====================================================================*/
#include "GaussianSplatData.h"

#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <zstd.h>
#if BUILD_TESTS
#include <utils/ConPrint.h>
#include <utils/MemMappedFile.h>
#include <utils/PlatformUtils.h>
#include <utils/TestUtils.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_map>


namespace
{
static const size_t MAX_HEADER_SIZE_B = 1024 * 1024;
static const size_t MAX_SPLAT_COUNT = 4000000;
static const float SH_C0 = 0.28209479177387814347f;
static const size_t SH_REST_COMPONENT_COUNTS[] = { 0, 9, 24, 45 };


static float readFloatLE(const uint8* p)
{
	float value;
	std::memcpy(&value, p, sizeof(value));
	return value;
}


static uint32 readUInt32LE(const uint8* p)
{
	return
		(uint32)p[0] |
		((uint32)p[1] << 8) |
		((uint32)p[2] << 16) |
		((uint32)p[3] << 24);
}


static uint64 readUInt64LE(const uint8* p)
{
	return
		(uint64)p[0] |
		((uint64)p[1] << 8) |
		((uint64)p[2] << 16) |
		((uint64)p[3] << 24) |
		((uint64)p[4] << 32) |
		((uint64)p[5] << 40) |
		((uint64)p[6] << 48) |
		((uint64)p[7] << 56);
}


static int32 readSignedFixed24LE(const uint8* p)
{
	int32 value = (int32)p[0] | ((int32)p[1] << 8) | ((int32)p[2] << 16);
	if(value & 0x00800000)
		value |= (int32)0xFF000000;
	return value;
}


static float clampFinite(float value, float lo, float hi, const char* field_name)
{
	if(!isFinite(value))
		throw glare::Exception(std::string("Gaussian splat has a non-finite ") + field_name + ".");
	return myClamp(value, lo, hi);
}


static float logistic(float value)
{
	value = clampFinite(value, -80.f, 80.f, "opacity");
	if(value >= 0.f)
		return 1.f / (1.f + std::exp(-value));
	const float e = std::exp(value);
	return e / (1.f + e);
}


static float shDCToDisplay(float dc)
{
	// SH-DC reconstructs the base display-space colour.  Do not clamp here:
	// directional terms are added by the renderer before the final clamp and
	// display-to-linear conversion.
	return 0.5f + SH_C0 * dc;
}


static size_t shRestComponentCount(const int degree)
{
	if(degree < 0 || degree > 3)
		throw glare::Exception("Gaussian splat SH degree must be between 0 and 3.");
	return SH_REST_COMPONENT_COUNTS[degree];
}


static void normaliseQuaternion(GaussianSplat& splat)
{
	const float len2 =
		splat.rotation_x * splat.rotation_x +
		splat.rotation_y * splat.rotation_y +
		splat.rotation_z * splat.rotation_z +
		splat.rotation_w * splat.rotation_w;
	if(!isFinite(len2) || len2 < 1.0e-20f)
	{
		splat.rotation_x = splat.rotation_y = splat.rotation_z = 0.f;
		splat.rotation_w = 1.f;
		return;
	}

	const float recip_len = 1.f / std::sqrt(len2);
	splat.rotation_x *= recip_len;
	splat.rotation_y *= recip_len;
	splat.rotation_z *= recip_len;
	splat.rotation_w *= recip_len;
}


static void enlargeAABBForSplat(js::AABBox& aabb, const GaussianSplat& splat)
{
	// Rotation can put any principal scale on any object-space axis.  The
	// length of the scale vector is a conservative bound for every axis.
	const float scale_radius = std::sqrt(
		splat.scale_x * splat.scale_x +
		splat.scale_y * splat.scale_y +
		splat.scale_z * splat.scale_z);
	const float radius = myClamp(3.f * scale_radius, 1.0e-6f, 1.0e6f);
	aabb.enlargeToHoldPoint(Vec4f(
		splat.position_x - radius,
		splat.position_y - radius,
		splat.position_z - radius,
		1.f));
	aabb.enlargeToHoldPoint(Vec4f(
		splat.position_x + radius,
		splat.position_y + radius,
		splat.position_z + radius,
		1.f));
}


enum class PlyScalarType
{
	Int8,
	UInt8,
	Int16,
	UInt16,
	Int32,
	UInt32,
	Float32,
	Float64
};


struct PlyProperty
{
	std::string name;
	PlyScalarType type;
	size_t offset;
	size_t size;
};


static bool parsePlyScalarType(const std::string& name, PlyScalarType& type_out, size_t& size_out)
{
	if(name == "char" || name == "int8")       { type_out = PlyScalarType::Int8;    size_out = 1; return true; }
	if(name == "uchar" || name == "uint8")     { type_out = PlyScalarType::UInt8;   size_out = 1; return true; }
	if(name == "short" || name == "int16")     { type_out = PlyScalarType::Int16;   size_out = 2; return true; }
	if(name == "ushort" || name == "uint16")   { type_out = PlyScalarType::UInt16;  size_out = 2; return true; }
	if(name == "int" || name == "int32")       { type_out = PlyScalarType::Int32;   size_out = 4; return true; }
	if(name == "uint" || name == "uint32")     { type_out = PlyScalarType::UInt32;  size_out = 4; return true; }
	if(name == "float" || name == "float32")   { type_out = PlyScalarType::Float32; size_out = 4; return true; }
	if(name == "double" || name == "float64")  { type_out = PlyScalarType::Float64; size_out = 8; return true; }
	return false;
}


template <class T>
static T readUnaligned(const uint8* p)
{
	T value;
	std::memcpy(&value, p, sizeof(value));
	return value;
}


static double readPlyScalar(const uint8* p, PlyScalarType type)
{
	switch(type)
	{
	case PlyScalarType::Int8:    return (double)readUnaligned<int8>(p);
	case PlyScalarType::UInt8:   return (double)readUnaligned<uint8>(p);
	case PlyScalarType::Int16:   return (double)readUnaligned<int16>(p);
	case PlyScalarType::UInt16:  return (double)readUnaligned<uint16>(p);
	case PlyScalarType::Int32:   return (double)readUnaligned<int32>(p);
	case PlyScalarType::UInt32:  return (double)readUnaligned<uint32>(p);
	case PlyScalarType::Float32: return (double)readUnaligned<float>(p);
	case PlyScalarType::Float64: return readUnaligned<double>(p);
	}
	return 0.0;
}


static const PlyProperty& requireProperty(const std::unordered_map<std::string, PlyProperty>& properties, const char* name)
{
	const auto it = properties.find(name);
	if(it == properties.end())
		throw glare::Exception(std::string("3DGS PLY is missing required vertex property '") + name + "'.");
	return it->second;
}


static GaussianSplatDataRef decodePly(ArrayRef<uint8> data)
{
	if(data.size() < 16 || std::memcmp(data.data(), "ply", 3) != 0)
		throw glare::Exception("3DGS PLY must start with a PLY header.");

	const size_t header_search_size = myMin(data.size(), MAX_HEADER_SIZE_B);
	const char* const chars = reinterpret_cast<const char*>(data.data());
	size_t header_end = std::string::npos;
	for(size_t i=0; i + 10 <= header_search_size; ++i)
	{
		if(std::memcmp(chars + i, "end_header", 10) == 0)
		{
			size_t next = i + 10;
			if(next < data.size() && chars[next] == '\r') ++next;
			if(next < data.size() && chars[next] == '\n') ++next;
			header_end = next;
			break;
		}
	}
	if(header_end == std::string::npos)
		throw glare::Exception("3DGS PLY header is incomplete or exceeds 1 MiB.");

	const std::string header(chars, chars + header_end);
	std::istringstream stream(header);
	std::string line;
	bool binary_little_endian = false;
	bool in_vertex_element = false;
	bool saw_payload_element_before_vertex = false;
	size_t vertex_count = 0;
	size_t vertex_stride = 0;
	std::vector<PlyProperty> vertex_properties;

	while(std::getline(stream, line))
	{
		if(!line.empty() && line.back() == '\r')
			line.pop_back();

		std::istringstream line_stream(line);
		std::string keyword;
		line_stream >> keyword;
		if(keyword == "format")
		{
			std::string format, version;
			line_stream >> format >> version;
			binary_little_endian = format == "binary_little_endian" && version == "1.0";
		}
		else if(keyword == "element")
		{
			std::string element_name;
			size_t element_count = 0;
			line_stream >> element_name >> element_count;
			if(element_name == "vertex")
			{
				in_vertex_element = true;
				vertex_count = element_count;
			}
			else
			{
				if(vertex_count == 0 && element_count > 0)
					saw_payload_element_before_vertex = true;
				in_vertex_element = false;
			}
		}
		else if(keyword == "property" && in_vertex_element)
		{
			std::string type_name;
			line_stream >> type_name;
			if(type_name == "list")
				throw glare::Exception("List-valued vertex properties are not supported in a 3DGS PLY.");

			std::string property_name;
			line_stream >> property_name;
			PlyScalarType scalar_type;
			size_t scalar_size = 0;
			if(property_name.empty() || !parsePlyScalarType(type_name, scalar_type, scalar_size))
				throw glare::Exception("Unsupported PLY vertex property type '" + type_name + "'.");

			PlyProperty property;
			property.name = property_name;
			property.type = scalar_type;
			property.offset = vertex_stride;
			property.size = scalar_size;
			vertex_properties.push_back(property);
			vertex_stride += scalar_size;
		}
	}

	if(!binary_little_endian)
		throw glare::Exception("Native Gaussian renderer currently supports only PLY 'format binary_little_endian 1.0'.");
	if(saw_payload_element_before_vertex)
		throw glare::Exception("PLY elements before 'vertex' are not supported by the native Gaussian decoder.");
	if(vertex_count == 0)
		throw glare::Exception("3DGS PLY contains no vertex splats.");
	if(vertex_count > MAX_SPLAT_COUNT)
		throw glare::Exception("3DGS PLY contains too many splats (maximum is " + toString(MAX_SPLAT_COUNT) + ").");
	if(vertex_stride == 0 || vertex_count > (std::numeric_limits<size_t>::max() - header_end) / vertex_stride)
		throw glare::Exception("3DGS PLY vertex payload size overflow.");
	const size_t payload_size = vertex_count * vertex_stride;
	if(header_end + payload_size > data.size())
		throw glare::Exception("3DGS PLY vertex payload is truncated.");

	std::unordered_map<std::string, PlyProperty> properties;
	for(const PlyProperty& property : vertex_properties)
		properties[property.name] = property;

	const PlyProperty& x_prop = requireProperty(properties, "x");
	const PlyProperty& y_prop = requireProperty(properties, "y");
	const PlyProperty& z_prop = requireProperty(properties, "z");
	const PlyProperty& opacity_prop = requireProperty(properties, "opacity");
	const PlyProperty& scale_0_prop = requireProperty(properties, "scale_0");
	const PlyProperty& scale_1_prop = requireProperty(properties, "scale_1");
	const PlyProperty& scale_2_prop = requireProperty(properties, "scale_2");
	const PlyProperty& rot_0_prop = requireProperty(properties, "rot_0");
	const PlyProperty& rot_1_prop = requireProperty(properties, "rot_1");
	const PlyProperty& rot_2_prop = requireProperty(properties, "rot_2");
	const PlyProperty& rot_3_prop = requireProperty(properties, "rot_3");
	const PlyProperty& dc_0_prop = requireProperty(properties, "f_dc_0");
	const PlyProperty& dc_1_prop = requireProperty(properties, "f_dc_1");
	const PlyProperty& dc_2_prop = requireProperty(properties, "f_dc_2");

	std::vector<const PlyProperty*> sh_properties;
	std::vector<bool> sh_property_seen;
	for(const PlyProperty& property : vertex_properties)
	{
		static const std::string SH_PROPERTY_PREFIX = "f_rest_";
		if(!hasPrefix(property.name, SH_PROPERTY_PREFIX))
			continue;

		const std::string index_s = property.name.substr(SH_PROPERTY_PREFIX.size());
		char* parse_end = NULL;
		const unsigned long index = std::strtoul(index_s.c_str(), &parse_end, 10);
		if(index_s.empty() || parse_end == index_s.c_str() || *parse_end != '\0' || index > 45)
			throw glare::Exception("3DGS PLY has a malformed spherical-harmonics property '" + property.name + "'.");
		if(sh_properties.size() <= index)
		{
			sh_properties.resize(index + 1, NULL);
			sh_property_seen.resize(index + 1, false);
		}
		if(sh_property_seen[index])
			throw glare::Exception("3DGS PLY has duplicate spherical-harmonics property '" + property.name + "'.");
		sh_properties[index] = &property;
		sh_property_seen[index] = true;
	}

	for(size_t i=0; i<sh_property_seen.size(); ++i)
		if(!sh_property_seen[i])
			throw glare::Exception("3DGS PLY spherical-harmonics properties must be consecutive from f_rest_0.");

	int sh_degree = -1;
	for(int degree=0; degree<=3; ++degree)
		if(sh_properties.size() == shRestComponentCount(degree))
			sh_degree = degree;
	if(sh_degree < 0)
		throw glare::Exception("3DGS PLY must contain exactly 0, 9, 24, or 45 f_rest spherical-harmonics properties.");

	auto read_property = [](const uint8* record, const PlyProperty& property) -> float
	{
		const double value = readPlyScalar(record + property.offset, property.type);
		if(!std::isfinite(value) || value < -std::numeric_limits<float>::max() || value > std::numeric_limits<float>::max())
			throw glare::Exception("3DGS PLY contains a non-finite or out-of-range vertex property.");
		return (float)value;
	};

	// A number of established Gaussian-splat exporters use +/-infinity for
	// fully opaque/fully transparent splats after opacity optimisation.  The
	// value is a logit, so the two infinities have an unambiguous, safe meaning.
	// Keep rejecting NaNs and non-finite values in all other vertex properties:
	// positions, scales, rotations and SH coefficients must remain finite.
	auto read_opacity_property = [](const uint8* record, const PlyProperty& property) -> float
	{
		const double value = readPlyScalar(record + property.offset, property.type);
		if(std::isnan(value) || value < -std::numeric_limits<float>::max() || value > std::numeric_limits<float>::max())
		{
			if(value == std::numeric_limits<double>::infinity())
				return 80.f; // logistic(80) is effectively fully opaque.
			if(value == -std::numeric_limits<double>::infinity())
				return -80.f; // logistic(-80) is effectively fully transparent.
			throw glare::Exception("3DGS PLY contains a NaN or out-of-range opacity value.");
		}
		return (float)value;
	};

	GaussianSplatDataRef result = new GaussianSplatData();
	result->source_format = GaussianSplatAsset::Format::Ply;
	result->sh_degree = sh_degree;
	result->splats.resize(vertex_count);
	result->spherical_harmonics.resize(vertex_count * sh_properties.size());
	result->aabb_os = js::AABBox::emptyAABBox();

	const uint8* const payload = data.data() + header_end;
	for(size_t i=0; i<vertex_count; ++i)
	{
		const uint8* const record = payload + i * vertex_stride;
		GaussianSplat& splat = result->splats[i];
		splat.position_x = clampFinite(read_property(record, x_prop), -1.0e8f, 1.0e8f, "position");
		splat.position_y = clampFinite(read_property(record, y_prop), -1.0e8f, 1.0e8f, "position");
		splat.position_z = clampFinite(read_property(record, z_prop), -1.0e8f, 1.0e8f, "position");
		splat.opacity = logistic(read_opacity_property(record, opacity_prop));

		splat.scale_x = std::exp(clampFinite(read_property(record, scale_0_prop), -20.f, 13.f, "scale"));
		splat.scale_y = std::exp(clampFinite(read_property(record, scale_1_prop), -20.f, 13.f, "scale"));
		splat.scale_z = std::exp(clampFinite(read_property(record, scale_2_prop), -20.f, 13.f, "scale"));
		splat.reserved_0 = 0.f;

		// INRIA PLY stores quaternion as w, x, y, z.
		splat.rotation_w = read_property(record, rot_0_prop);
		splat.rotation_x = read_property(record, rot_1_prop);
		splat.rotation_y = read_property(record, rot_2_prop);
		splat.rotation_z = read_property(record, rot_3_prop);
		normaliseQuaternion(splat);

		splat.colour_r = shDCToDisplay(read_property(record, dc_0_prop));
		splat.colour_g = shDCToDisplay(read_property(record, dc_1_prop));
		splat.colour_b = shDCToDisplay(read_property(record, dc_2_prop));
		splat.reserved_1 = 0.f;

		for(size_t component=0; component<sh_properties.size(); ++component)
			result->spherical_harmonics[i * sh_properties.size() + component] =
				read_property(record, *sh_properties[component]);

		enlargeAABBForSplat(result->aabb_os, splat);
	}

	return result;
}


static GaussianSplatDataRef decodeSplat(ArrayRef<uint8> data)
{
	static const size_t RECORD_SIZE = 32;
	if(data.empty() || data.size() % RECORD_SIZE != 0)
		throw glare::Exception("A .splat file must contain one or more complete 32-byte splat records.");

	const size_t splat_count = data.size() / RECORD_SIZE;
	if(splat_count > MAX_SPLAT_COUNT)
		throw glare::Exception(".splat contains too many splats (maximum is " + toString(MAX_SPLAT_COUNT) + ").");

	GaussianSplatDataRef result = new GaussianSplatData();
	result->source_format = GaussianSplatAsset::Format::Splat;
	result->sh_degree = 0;
	result->splats.resize(splat_count);
	result->aabb_os = js::AABBox::emptyAABBox();

	for(size_t i=0; i<splat_count; ++i)
	{
		const uint8* const record = data.data() + i * RECORD_SIZE;
		GaussianSplat& splat = result->splats[i];
		splat.position_x = clampFinite(readFloatLE(record + 0), -1.0e8f, 1.0e8f, "position");
		splat.position_y = clampFinite(readFloatLE(record + 4), -1.0e8f, 1.0e8f, "position");
		splat.position_z = clampFinite(readFloatLE(record + 8), -1.0e8f, 1.0e8f, "position");
		splat.scale_x = myClamp(std::fabs(clampFinite(readFloatLE(record + 12), -1.0e6f, 1.0e6f, "scale")), 1.0e-6f, 1.0e6f);
		splat.scale_y = myClamp(std::fabs(clampFinite(readFloatLE(record + 16), -1.0e6f, 1.0e6f, "scale")), 1.0e-6f, 1.0e6f);
		splat.scale_z = myClamp(std::fabs(clampFinite(readFloatLE(record + 20), -1.0e6f, 1.0e6f, "scale")), 1.0e-6f, 1.0e6f);
		splat.reserved_0 = 0.f;

		splat.colour_r = (float)record[24] * (1.f / 255.f);
		splat.colour_g = (float)record[25] * (1.f / 255.f);
		splat.colour_b = (float)record[26] * (1.f / 255.f);
		splat.opacity = (float)record[27] * (1.f / 255.f);

		// The common .splat stream stores quaternion as w, x, y, z bytes.
		splat.rotation_w = ((float)record[28] - 128.f) * (1.f / 128.f);
		splat.rotation_x = ((float)record[29] - 128.f) * (1.f / 128.f);
		splat.rotation_y = ((float)record[30] - 128.f) * (1.f / 128.f);
		splat.rotation_z = ((float)record[31] - 128.f) * (1.f / 128.f);
		splat.reserved_1 = 0.f;
		normaliseQuaternion(splat);
		enlargeAABBForSplat(result->aabb_os, splat);
	}

	return result;
}


static GaussianSplatDataRef decodeSpz(ArrayRef<uint8> data)
{
	static const uint32 SPZ_MAGIC = 0x5053474e; // 'NGSP' in little-endian byte order.
	static const uint32 SPZ_VERSION_ZSTD = 4;
	static const size_t HEADER_SIZE = 32;
	static const size_t TOC_ENTRY_SIZE = 16;
	static const size_t HARMONICS_COMPONENT_COUNT[] = { 0, 9, 24, 45, 72 };

	if(data.size() < HEADER_SIZE || readUInt32LE(data.data()) != SPZ_MAGIC)
		throw glare::Exception("Invalid .spz header.");

	const uint32 version = readUInt32LE(data.data() + 4);
	if(version != SPZ_VERSION_ZSTD)
		throw glare::Exception("Native .spz decoder currently supports the ZSTD-stream version 4 container; version " + toString(version) + " can use the CEF conversion fallback.");

	const size_t splat_count = readUInt32LE(data.data() + 8);
	const uint8 sh_degree = data[12];
	const uint8 fractional_bits = data[13];
	const uint8 num_streams = data[15];
	const size_t toc_offset = readUInt32LE(data.data() + 16);
	if(splat_count == 0 || splat_count > MAX_SPLAT_COUNT)
		throw glare::Exception(".spz has an invalid splat count.");
	if(sh_degree > 3)
		throw glare::Exception("Native .spz decoder supports SH degree 0 through 3.");
	if(fractional_bits > 30)
		throw glare::Exception(".spz has an invalid fixed-point fractional bit count.");

	const size_t expected_num_streams = sh_degree == 0 ? 5 : 6;
	if(num_streams != expected_num_streams || toc_offset < HEADER_SIZE)
		throw glare::Exception(".spz has an invalid stream table.");
	if(toc_offset > data.size() || num_streams > (data.size() - toc_offset) / TOC_ENTRY_SIZE)
		throw glare::Exception(".spz stream table is truncated.");

	const size_t harmonics_count = HARMONICS_COMPONENT_COUNT[sh_degree];
	if(splat_count > std::numeric_limits<size_t>::max() / myMax((size_t)9, harmonics_count))
		throw glare::Exception(".spz stream size overflow.");
	std::vector<size_t> expected_sizes;
	expected_sizes.push_back(splat_count * 9);
	expected_sizes.push_back(splat_count);
	expected_sizes.push_back(splat_count * 3);
	expected_sizes.push_back(splat_count * 3);
	expected_sizes.push_back(splat_count * 4);
	if(sh_degree > 0)
		expected_sizes.push_back(splat_count * harmonics_count);

	const size_t data_start = toc_offset + num_streams * TOC_ENTRY_SIZE;
	size_t compressed_offset = data_start;
	std::vector<std::vector<uint8>> streams(num_streams);
	for(size_t i=0; i<num_streams; ++i)
	{
		const uint8* toc_entry = data.data() + toc_offset + i * TOC_ENTRY_SIZE;
		const uint64 compressed_size_64 = readUInt64LE(toc_entry);
		const uint64 uncompressed_size_64 = readUInt64LE(toc_entry + 8);
		if(uncompressed_size_64 != expected_sizes[i] ||
			compressed_size_64 > std::numeric_limits<size_t>::max())
			throw glare::Exception(".spz stream table contains an invalid stream size.");
		const size_t compressed_size = (size_t)compressed_size_64;
		if(compressed_offset > data.size() || compressed_size > data.size() - compressed_offset)
			throw glare::Exception(".spz compressed stream is truncated.");

		streams[i].resize(expected_sizes[i]);
		const size_t decompressed_size = ZSTD_decompress(
			streams[i].data(),
			streams[i].size(),
			data.data() + compressed_offset,
			compressed_size
		);
		if(ZSTD_isError(decompressed_size))
			throw glare::Exception(std::string("Could not decompress .spz stream: ") + ZSTD_getErrorName(decompressed_size));
		if(decompressed_size != expected_sizes[i])
			throw glare::Exception(".spz stream decompressed to an unexpected size.");
		compressed_offset += compressed_size;
	}

	const std::vector<uint8>& positions = streams[0];
	const std::vector<uint8>& alphas = streams[1];
	const std::vector<uint8>& colours = streams[2];
	const std::vector<uint8>& scales = streams[3];
	const std::vector<uint8>& rotations = streams[4];
	const std::vector<uint8>* harmonics = sh_degree > 0 ? &streams[5] : NULL;

	GaussianSplatDataRef result = new GaussianSplatData();
	result->source_format = GaussianSplatAsset::Format::SPZ;
	result->sh_degree = (int)sh_degree;
	result->splats.resize(splat_count);
	result->spherical_harmonics.resize(splat_count * harmonics_count);
	result->aabb_os = js::AABBox::emptyAABBox();
	const float position_scale = 1.f / (float)(1u << fractional_bits);
	static const float INV_SPZ_COLOUR_SCALE = 1.f / 0.15f;
	static const float INV_SQRT_2 = 0.7071067811865475244f;

	for(size_t i=0; i<splat_count; ++i)
	{
		GaussianSplat& splat = result->splats[i];
		splat.position_x = (float)readSignedFixed24LE(positions.data() + i * 9 + 0) * position_scale;
		splat.position_y = (float)readSignedFixed24LE(positions.data() + i * 9 + 3) * position_scale;
		splat.position_z = (float)readSignedFixed24LE(positions.data() + i * 9 + 6) * position_scale;
		splat.opacity = (float)alphas[i] * (1.f / 255.f);

		splat.scale_x = std::exp((float)scales[i * 3 + 0] * (1.f / 16.f) - 10.f);
		splat.scale_y = std::exp((float)scales[i * 3 + 1] * (1.f / 16.f) - 10.f);
		splat.scale_z = std::exp((float)scales[i * 3 + 2] * (1.f / 16.f) - 10.f);
		splat.reserved_0 = 0.f;

		float quaternion[4] = { 0.f, 0.f, 0.f, 0.f }; // x, y, z, w
		uint32 packed = readUInt32LE(rotations.data() + i * 4);
		const uint32 largest = packed >> 30;
		float sum_squares = 0.f;
		for(int component=3; component>=0; --component)
		{
			if((uint32)component != largest)
			{
				const uint32 magnitude = packed & 511u;
				const bool negative = ((packed >> 9) & 1u) != 0;
				packed >>= 10;
				float value = INV_SQRT_2 * (float)magnitude * (1.f / 511.f);
				if(negative)
					value = -value;
				quaternion[component] = value;
				sum_squares += value * value;
			}
		}
		quaternion[largest] = std::sqrt(myMax(0.f, 1.f - sum_squares));
		splat.rotation_x = quaternion[0];
		splat.rotation_y = quaternion[1];
		splat.rotation_z = quaternion[2];
		splat.rotation_w = quaternion[3];
		normaliseQuaternion(splat);

		const float dc_r = ((float)colours[i * 3 + 0] * (1.f / 255.f) - 0.5f) * INV_SPZ_COLOUR_SCALE;
		const float dc_g = ((float)colours[i * 3 + 1] * (1.f / 255.f) - 0.5f) * INV_SPZ_COLOUR_SCALE;
		const float dc_b = ((float)colours[i * 3 + 2] * (1.f / 255.f) - 0.5f) * INV_SPZ_COLOUR_SCALE;
		splat.colour_r = shDCToDisplay(dc_r);
		splat.colour_g = shDCToDisplay(dc_g);
		splat.colour_b = shDCToDisplay(dc_b);
		splat.reserved_1 = 0.f;

		const size_t coefficients_per_channel = harmonics_count / 3;
		for(size_t component=0; component<harmonics_count; ++component)
		{
			const size_t channel = component % 3;
			const size_t coefficient = component / 3;
			result->spherical_harmonics[i * harmonics_count + channel * coefficients_per_channel + coefficient] =
				((float)(*harmonics)[i * harmonics_count + component] - 128.f) * (1.f / 128.f);
		}

		enlargeAABBForSplat(result->aabb_os, splat);
	}

	return result;
}
}


GaussianSplatData::GaussianSplatData()
:	sh_degree(0),
	aabb_os(js::AABBox::emptyAABBox()),
	source_format(GaussianSplatAsset::Format::Unknown)
{}


GaussianSplatRenderSettings::GaussianSplatRenderSettings()
:	opacity_multiplier(1.0f),
	brightness(1.0f),
	radius_multiplier(1.0f),
	saturation(1.0f),
	contrast(1.0f),
	alpha_cutoff(1.0f / 255.0f),
	minimum_source_opacity(0.0f),
	sh_degree_override(-1)
{}


bool GaussianSplatRenderSettings::isDefault() const
{
	return std::fabs(opacity_multiplier - 1.0f) < 1.0e-6f &&
		std::fabs(brightness - 1.0f) < 1.0e-6f &&
		std::fabs(radius_multiplier - 1.0f) < 1.0e-6f &&
		std::fabs(saturation - 1.0f) < 1.0e-6f &&
		std::fabs(contrast - 1.0f) < 1.0e-6f &&
		std::fabs(alpha_cutoff - (1.0f / 255.0f)) < 1.0e-6f &&
		std::fabs(minimum_source_opacity) < 1.0e-6f &&
		sh_degree_override == -1;
}


std::string GaussianSplatRenderSettings::cacheKeySuffix() const
{
	return "#gs:opacity=" + toString(opacity_multiplier) +
		";brightness=" + toString(brightness) +
		";radius=" + toString(radius_multiplier) +
		";saturation=" + toString(saturation) +
		";contrast=" + toString(contrast) +
		";cutoff=" + toString(alpha_cutoff) +
		";source_opacity=" + toString(minimum_source_opacity) +
		";sh_degree=" + toString(sh_degree_override);
}


static const char* GAUSSIAN_SPLAT_SETTINGS_PREFIX = "gaussian_splat_settings_v1";


bool GaussianSplatRenderSettings::isSettingsContent(const std::string& content)
{
	return hasPrefix(content, GAUSSIAN_SPLAT_SETTINGS_PREFIX);
}


GaussianSplatRenderSettings GaussianSplatRenderSettings::fromContent(const std::string& content)
{
	GaussianSplatRenderSettings settings;
	if(!isSettingsContent(content))
		return settings;

	std::istringstream stream(content);
	std::string line;
	while(std::getline(stream, line))
	{
		if(!line.empty() && line.back() == '\r')
			line.pop_back();

		const size_t eq_pos = line.find('=');
		if(eq_pos == std::string::npos)
			continue;
		const std::string key = line.substr(0, eq_pos);
		const std::string value_s = line.substr(eq_pos + 1);
		const float value = (float)::atof(value_s.c_str());

		if(key == "opacity_multiplier")
			settings.opacity_multiplier = myClamp(value, 0.05f, 32.0f);
		else if(key == "brightness")
			settings.brightness = myClamp(value, 0.05f, 4.0f);
		else if(key == "radius_multiplier")
			settings.radius_multiplier = myClamp(value, 0.10f, 4.0f);
		else if(key == "saturation")
			settings.saturation = myClamp(value, 0.0f, 3.0f);
		else if(key == "contrast")
			settings.contrast = myClamp(value, 0.10f, 4.0f);
		else if(key == "alpha_cutoff")
			settings.alpha_cutoff = myClamp(value, 0.0f, 0.25f);
		else if(key == "minimum_source_opacity")
			settings.minimum_source_opacity = myClamp(value, 0.0f, 1.0f);
		else if(key == "sh_degree_override")
			settings.sh_degree_override = myClamp((int)::atoi(value_s.c_str()), -1, 3);
	}

	return settings;
}


std::string GaussianSplatRenderSettings::serialiseToContent(const GaussianSplatRenderSettings& settings_)
{
	GaussianSplatRenderSettings settings = settings_;
	settings.opacity_multiplier = myClamp(settings.opacity_multiplier, 0.05f, 32.0f);
	settings.brightness = myClamp(settings.brightness, 0.05f, 4.0f);
	settings.radius_multiplier = myClamp(settings.radius_multiplier, 0.10f, 4.0f);
	settings.saturation = myClamp(settings.saturation, 0.0f, 3.0f);
	settings.contrast = myClamp(settings.contrast, 0.10f, 4.0f);
	settings.alpha_cutoff = myClamp(settings.alpha_cutoff, 0.0f, 0.25f);
	settings.minimum_source_opacity = myClamp(settings.minimum_source_opacity, 0.0f, 1.0f);
	settings.sh_degree_override = myClamp(settings.sh_degree_override, -1, 3);

	return std::string(GAUSSIAN_SPLAT_SETTINGS_PREFIX) + "\n" +
		"opacity_multiplier=" + toString(settings.opacity_multiplier) + "\n" +
		"brightness=" + toString(settings.brightness) + "\n" +
		"radius_multiplier=" + toString(settings.radius_multiplier) + "\n" +
		"saturation=" + toString(settings.saturation) + "\n" +
		"contrast=" + toString(settings.contrast) + "\n" +
		"alpha_cutoff=" + toString(settings.alpha_cutoff) + "\n" +
		"minimum_source_opacity=" + toString(settings.minimum_source_opacity) + "\n" +
		"sh_degree_override=" + toString(settings.sh_degree_override) + "\n";
}


GaussianSplatDataRef GaussianSplatRenderSettings::applyToData(const GaussianSplatData& source, const GaussianSplatRenderSettings& settings_)
{
	GaussianSplatRenderSettings settings = settings_;
	settings.opacity_multiplier = myClamp(settings.opacity_multiplier, 0.05f, 32.0f);
	settings.brightness = myClamp(settings.brightness, 0.05f, 4.0f);
	settings.radius_multiplier = myClamp(settings.radius_multiplier, 0.10f, 4.0f);
	settings.saturation = myClamp(settings.saturation, 0.0f, 3.0f);
	settings.contrast = myClamp(settings.contrast, 0.10f, 4.0f);
	settings.alpha_cutoff = myClamp(settings.alpha_cutoff, 0.0f, 0.25f);
	settings.minimum_source_opacity = myClamp(settings.minimum_source_opacity, 0.0f, 1.0f);
	settings.sh_degree_override = myClamp(settings.sh_degree_override, -1, 3);

	if(source.sh_degree < 0 || source.sh_degree > 3)
		throw glare::Exception("Gaussian splat source has an invalid SH degree.");
	const size_t source_sh_component_count = shRestComponentCount(source.sh_degree);
	if(source.spherical_harmonics.size() != source.splats.size() * source_sh_component_count)
		throw glare::Exception("Gaussian splat source has an invalid spherical-harmonics payload.");

	const int result_sh_degree = settings.sh_degree_override < 0 ?
		source.sh_degree :
		myMin(source.sh_degree, settings.sh_degree_override);
	const size_t result_sh_component_count = shRestComponentCount(result_sh_degree);
	const size_t source_coefficients_per_channel = source_sh_component_count / 3;
	const size_t result_coefficients_per_channel = result_sh_component_count / 3;

	GaussianSplatDataRef result = new GaussianSplatData();
	result->source_format = source.source_format;
	result->sh_degree = result_sh_degree;
	result->splats = source.splats;
	result->spherical_harmonics.resize(source.splats.size() * result_sh_component_count);
	result->aabb_os = js::AABBox::emptyAABBox();

	for(size_t splat_index=0; splat_index<result->splats.size(); ++splat_index)
	{
		GaussianSplat& splat = result->splats[splat_index];
		const float source_opacity = myClamp(splat.opacity, 0.f, 1.f);
		if(source_opacity < settings.minimum_source_opacity)
			splat.opacity = 0.f;
		else
			splat.opacity = myClamp(
				1.0f - std::pow(myMax(0.f, 1.0f - source_opacity), settings.opacity_multiplier),
				0.f,
				1.f
			);
		splat.scale_x = myClamp(splat.scale_x * settings.radius_multiplier, 1.0e-8f, 1.0e8f);
		splat.scale_y = myClamp(splat.scale_y * settings.radius_multiplier, 1.0e-8f, 1.0e8f);
		splat.scale_z = myClamp(splat.scale_z * settings.radius_multiplier, 1.0e-8f, 1.0e8f);

		for(size_t channel=0; channel<3; ++channel)
			for(size_t coefficient=0; coefficient<result_coefficients_per_channel; ++coefficient)
				result->spherical_harmonics[
					splat_index * result_sh_component_count +
					channel * result_coefficients_per_channel +
					coefficient
				] = source.spherical_harmonics[
					splat_index * source_sh_component_count +
					channel * source_coefficients_per_channel +
					coefficient
				];

		float r = splat.colour_r;
		float g = splat.colour_g;
		float b = splat.colour_b;
		const float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
		r = luminance + (r - luminance) * settings.saturation;
		g = luminance + (g - luminance) * settings.saturation;
		b = luminance + (b - luminance) * settings.saturation;
		r = 0.5f + (r - 0.5f) * settings.contrast;
		g = 0.5f + (g - 0.5f) * settings.contrast;
		b = 0.5f + (b - 0.5f) * settings.contrast;
		splat.colour_r = r * settings.brightness;
		splat.colour_g = g * settings.brightness;
		splat.colour_b = b * settings.brightness;

		for(size_t coefficient=0; coefficient<result_coefficients_per_channel; ++coefficient)
		{
			const size_t base = splat_index * result_sh_component_count;
			float sh_r = result->spherical_harmonics[base + coefficient];
			float sh_g = result->spherical_harmonics[base + result_coefficients_per_channel + coefficient];
			float sh_b = result->spherical_harmonics[base + 2 * result_coefficients_per_channel + coefficient];
			const float sh_luminance = 0.2126f * sh_r + 0.7152f * sh_g + 0.0722f * sh_b;
			sh_r = sh_luminance + (sh_r - sh_luminance) * settings.saturation;
			sh_g = sh_luminance + (sh_g - sh_luminance) * settings.saturation;
			sh_b = sh_luminance + (sh_b - sh_luminance) * settings.saturation;
			const float sh_scale = settings.contrast * settings.brightness;
			result->spherical_harmonics[base + coefficient] = sh_r * sh_scale;
			result->spherical_harmonics[base + result_coefficients_per_channel + coefficient] = sh_g * sh_scale;
			result->spherical_harmonics[base + 2 * result_coefficients_per_channel + coefficient] = sh_b * sh_scale;
		}

		splat.reserved_1 = settings.alpha_cutoff;
		enlargeAABBForSplat(result->aabb_os, splat);
	}

	return result;
}


GaussianSplatDataRef GaussianSplatDecoder::decode(const string_view source_name, ArrayRef<uint8> data)
{
	switch(GaussianSplatAsset::detectFormat(source_name))
	{
	case GaussianSplatAsset::Format::Ply:
		return decodePly(data);
	case GaussianSplatAsset::Format::Splat:
		return decodeSplat(data);
	case GaussianSplatAsset::Format::CompressedPly:
		throw glare::Exception("'.compressed.ply' is recognised, but its native decoder is not enabled yet. Convert it to binary little-endian 3DGS PLY or .splat for this build.");
	case GaussianSplatAsset::Format::KSplat:
		throw glare::Exception("'.ksplat' is recognised, but its native decoder is not enabled yet. Convert it to binary little-endian 3DGS PLY or .splat for this build.");
	case GaussianSplatAsset::Format::SPZ:
		return decodeSpz(data);
	case GaussianSplatAsset::Format::SOG:
		throw glare::Exception("'.sog' is recognised, but its native decoder is not enabled yet. Convert it to binary little-endian 3DGS PLY or .splat for this build.");
	case GaussianSplatAsset::Format::LCC:
	case GaussianSplatAsset::Format::LCC2:
		throw glare::Exception("LCC Gaussian splat containers are recognised, but their native decoder is not enabled yet. Convert the asset to binary little-endian 3DGS PLY or .splat for this build.");
	case GaussianSplatAsset::Format::Unknown:
	default:
		throw glare::Exception("Unsupported Gaussian splat container '" +
			getExtension(std::string(source_name.data(), source_name.size())) + "'.");
	}
}


#if BUILD_TESTS


void GaussianSplatDecoder::test()
{
	conPrint("GaussianSplatDecoder::test()");

	const std::string header =
		"ply\n"
		"format binary_little_endian 1.0\n"
		"element vertex 1\n"
		"property float x\n"
		"property float y\n"
		"property float z\n"
		"property float opacity\n"
		"property float scale_0\n"
		"property float scale_1\n"
		"property float scale_2\n"
		"property float rot_0\n"
		"property float rot_1\n"
		"property float rot_2\n"
		"property float rot_3\n"
		"property float f_dc_0\n"
		"property float f_dc_1\n"
		"property float f_dc_2\n"
		"property float f_rest_0\n"
		"property float f_rest_1\n"
		"property float f_rest_2\n"
		"property float f_rest_3\n"
		"property float f_rest_4\n"
		"property float f_rest_5\n"
		"property float f_rest_6\n"
		"property float f_rest_7\n"
		"property float f_rest_8\n"
		"end_header\n";
	std::vector<uint8> ply_bytes(header.begin(), header.end());
	auto append_float = [&ply_bytes](float value)
	{
		const size_t old_size = ply_bytes.size();
		ply_bytes.resize(old_size + sizeof(float));
		std::memcpy(ply_bytes.data() + old_size, &value, sizeof(value));
	};
	append_float(1.f);
	append_float(2.f);
	append_float(3.f);
	append_float(0.f);
	append_float(0.f);
	append_float(0.f);
	append_float(0.f);
	append_float(1.f);
	append_float(0.f);
	append_float(0.f);
	append_float(0.f);
	append_float(0.f);
	append_float(0.f);
	append_float(0.f);
	for(int i=0; i<9; ++i)
		append_float((float)(i + 1) * 0.01f);

	const GaussianSplatDataRef ply = decode("scene.ply", ArrayRef<uint8>(ply_bytes.data(), ply_bytes.size()));
	testAssert(ply->source_format == GaussianSplatAsset::Format::Ply);
	testAssert(ply->sh_degree == 1);
	testAssert(ply->spherical_harmonics.size() == 9);
	testAssert(ply->splats.size() == 1);
	testAssert(epsEqual(ply->splats[0].position_x, 1.f));
	testAssert(epsEqual(ply->splats[0].position_y, 2.f));
	testAssert(epsEqual(ply->splats[0].position_z, 3.f));
	testAssert(epsEqual(ply->splats[0].opacity, 0.5f));
	testAssert(epsEqual(ply->splats[0].scale_x, 1.f));
	testAssert(epsEqual(ply->splats[0].rotation_w, 1.f));
	testAssert(epsEqual(ply->splats[0].colour_r, 0.5f));
	testAssert(epsEqual(ply->spherical_harmonics[0], 0.01f));
	testAssert(epsEqual(ply->spherical_harmonics[8], 0.09f));

	// Infinity is a valid optimised opacity logit in real-world 3DGS PLY files.
	std::vector<uint8> infinite_opacity_ply = ply_bytes;
	const float infinity = std::numeric_limits<float>::infinity();
	std::memcpy(infinite_opacity_ply.data() + header.size() + 3 * sizeof(float), &infinity, sizeof(infinity));
	const GaussianSplatDataRef infinite_opacity = decode(
		"infinite-opacity.ply", ArrayRef<uint8>(infinite_opacity_ply.data(), infinite_opacity_ply.size())
	);
	testAssert(infinite_opacity->splats[0].opacity > 0.9999f);

	const float negative_infinity = -std::numeric_limits<float>::infinity();
	std::memcpy(infinite_opacity_ply.data() + header.size() + 3 * sizeof(float), &negative_infinity, sizeof(negative_infinity));
	const GaussianSplatDataRef negative_infinite_opacity = decode(
		"negative-infinite-opacity.ply", ArrayRef<uint8>(infinite_opacity_ply.data(), infinite_opacity_ply.size())
	);
	testAssert(negative_infinite_opacity->splats[0].opacity < 0.0001f);

	std::vector<uint8> splat_bytes(32, 0);
	auto write_float = [&splat_bytes](size_t offset, float value)
	{
		std::memcpy(splat_bytes.data() + offset, &value, sizeof(value));
	};
	write_float(0, -1.f);
	write_float(4, 4.f);
	write_float(8, 2.f);
	write_float(12, 0.25f);
	write_float(16, 0.5f);
	write_float(20, 1.f);
	splat_bytes[24] = 255;
	splat_bytes[25] = 0;
	splat_bytes[26] = 128;
	splat_bytes[27] = 192;
	splat_bytes[28] = 255;
	splat_bytes[29] = 128;
	splat_bytes[30] = 128;
	splat_bytes[31] = 128;

	const GaussianSplatDataRef splat = decode("scene.splat", ArrayRef<uint8>(splat_bytes.data(), splat_bytes.size()));
	testAssert(splat->source_format == GaussianSplatAsset::Format::Splat);
	testAssert(splat->sh_degree == 0);
	testAssert(splat->spherical_harmonics.empty());
	testAssert(splat->splats.size() == 1);
	testAssert(epsEqual(splat->splats[0].position_x, -1.f));
	testAssert(epsEqual(splat->splats[0].scale_y, 0.5f));
	testAssert(epsEqual(splat->splats[0].colour_r, 1.f));
	testAssert(epsEqual(splat->splats[0].colour_g, 0.f));
	testAssert(epsEqual(splat->splats[0].colour_b, 128.f / 255.f));
	testAssert(epsEqual(splat->splats[0].opacity, 192.f / 255.f));
	testAssert(epsEqual(splat->splats[0].rotation_w, 1.f));

	GaussianSplatRenderSettings default_settings;
	testAssert(default_settings.isDefault());
	testAssert(epsEqual(default_settings.opacity_multiplier, 1.f));
	const GaussianSplatDataRef default_tuned = GaussianSplatRenderSettings::applyToData(*ply, default_settings);
	testAssert(default_tuned->sh_degree == 1);
	testAssert(default_tuned->spherical_harmonics.size() == 9);
	testAssert(epsEqual(default_tuned->splats[0].opacity, 0.5f));
	testAssert(epsEqual(default_tuned->spherical_harmonics[8], 0.09f));

	GaussianSplatRenderSettings tuned_settings;
	tuned_settings.opacity_multiplier = 2.f;
	tuned_settings.sh_degree_override = 0;
	const GaussianSplatDataRef tuned = GaussianSplatRenderSettings::applyToData(*ply, tuned_settings);
	testAssert(tuned->sh_degree == 0);
	testAssert(tuned->spherical_harmonics.empty());
	testAssert(epsEqual(tuned->splats[0].opacity, 0.75f));

	tuned_settings.minimum_source_opacity = 0.6f;
	const GaussianSplatDataRef filtered = GaussianSplatRenderSettings::applyToData(*ply, tuned_settings);
	testAssert(epsEqual(filtered->splats[0].opacity, 0.f));

	tuned_settings.minimum_source_opacity = 0.1f;
	tuned_settings.sh_degree_override = 1;
	const std::string settings_content = GaussianSplatRenderSettings::serialiseToContent(tuned_settings);
	const GaussianSplatRenderSettings parsed_settings = GaussianSplatRenderSettings::fromContent(settings_content);
	testAssert(epsEqual(parsed_settings.minimum_source_opacity, 0.1f));
	testAssert(parsed_settings.sh_degree_override == 1);
	testAssert(epsEqual(parsed_settings.opacity_multiplier, 2.f));

	const GaussianSplatRenderSettings old_v1_settings =
		GaussianSplatRenderSettings::fromContent("gaussian_splat_settings_v1\nbrightness=0.8\n");
	testAssert(epsEqual(old_v1_settings.opacity_multiplier, 1.f));
	testAssert(epsEqual(old_v1_settings.minimum_source_opacity, 0.f));
	testAssert(old_v1_settings.sh_degree_override == -1);

	bool got_extensible_error = false;
	try
	{
		decode("scene.sog", ArrayRef<uint8>(splat_bytes.data(), splat_bytes.size()));
	}
	catch(glare::Exception& e)
	{
		got_extensible_error = e.what().find("recognised") != std::string::npos;
	}
	testAssert(got_extensible_error);

	std::string external_test_asset;
	try
	{
		external_test_asset = PlatformUtils::getEnvironmentVariable("GAUSSIAN_SPLAT_TEST_ASSET");
	}
	catch(glare::Exception&)
	{}
	if(!external_test_asset.empty())
	{
		MemMappedFile file(external_test_asset);
		const GaussianSplatDataRef external_data = decode(
			external_test_asset,
			ArrayRef<uint8>((const uint8*)file.fileData(), file.fileSize())
		);
		testAssert(external_data.nonNull() && !external_data->splats.empty());
		conPrint("Decoded external Gaussian test asset '" + external_test_asset + "': " +
			toString(external_data->splats.size()) + " splats.");
	}

	conPrint("GaussianSplatDecoder::test() done");
}


#endif // BUILD_TESTS
