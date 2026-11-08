/*=====================================================================
ScientificObjectTableCodec.h
----------------------------
Compact storage for scientific editor tables inside WorldObject content.
=====================================================================*/
#pragma once

#include <Base64.h>
#include <Exception.h>
#include <zstd.h>
#include <algorithm>
#include <stdint.h>
#include <string>
#include <vector>

namespace ScientificObjectTableCodec
{
static const char* const FIELD_NAME = "scientific_tables_zstd_base64";
static const size_t MAX_DECOMPRESSED_SIZE = 4 * 1024 * 1024;

inline void appendLength(std::string& data, const uint32_t length)
{
	data.push_back((char)(length >> 24));
	data.push_back((char)(length >> 16));
	data.push_back((char)(length >> 8));
	data.push_back((char)length);
}

inline uint32_t readLength(const std::string& data, const size_t offset)
{
	return ((uint32_t)(unsigned char)data[offset] << 24) |
		((uint32_t)(unsigned char)data[offset + 1] << 16) |
		((uint32_t)(unsigned char)data[offset + 2] << 8) |
		(uint32_t)(unsigned char)data[offset + 3];
}

inline std::string encode(const std::string& atom_table, const std::string& bond_table,
	const std::string& point_table, const std::string& value_table, const std::string& property_table)
{
	const std::string* const tables[] = { &atom_table, &bond_table, &point_table, &value_table, &property_table };
	std::string packed;
	for(size_t i=0; i<5; ++i)
	{
		if(tables[i]->size() > UINT32_MAX || packed.size() + tables[i]->size() + 4 > MAX_DECOMPRESSED_SIZE)
			throw glare::Exception("Scientific object tables exceed the 4 MB decoded content limit.");
		appendLength(packed, (uint32_t)tables[i]->size());
		packed.append(*tables[i]);
	}
	const size_t bound = ZSTD_compressBound(packed.size());
	std::vector<unsigned char> compressed(bound);
	const size_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(), packed.data(), packed.size(), ZSTD_CLEVEL_DEFAULT);
	if(ZSTD_isError(compressed_size))
		throw glare::Exception(std::string("Could not compress scientific object tables: ") + ZSTD_getErrorName(compressed_size));
	std::string encoded;
	Base64::encode(compressed.data(), compressed_size, encoded);
	return encoded;
}

inline void decode(const std::string& encoded, std::string& atom_table, std::string& bond_table,
	std::string& point_table, std::string& value_table, std::string& property_table)
{
	std::vector<unsigned char> compressed;
	Base64::decode(encoded, compressed);
	if(compressed.empty()) throw glare::Exception("Compressed scientific object tables are empty.");
	const unsigned long long frame_size = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
	if(frame_size == ZSTD_CONTENTSIZE_ERROR || frame_size == ZSTD_CONTENTSIZE_UNKNOWN || frame_size < 20 || frame_size > MAX_DECOMPRESSED_SIZE)
		throw glare::Exception("Compressed scientific object tables have an invalid or oversized frame.");
	std::string packed((size_t)frame_size, '\0');
	const size_t decoded_size = ZSTD_decompress(&packed[0], packed.size(), compressed.data(), compressed.size());
	if(ZSTD_isError(decoded_size) || decoded_size != packed.size())
		throw glare::Exception("Could not decompress scientific object tables.");
	std::string* const tables[] = { &atom_table, &bond_table, &point_table, &value_table, &property_table };
	size_t offset = 0;
	for(size_t i=0; i<5; ++i)
	{
		if(offset + 4 > packed.size()) throw glare::Exception("Compressed scientific object table header is truncated.");
		const uint32_t length = readLength(packed, offset);
		offset += 4;
		if((size_t)length > packed.size() - offset) throw glare::Exception("Compressed scientific object table is truncated.");
		tables[i]->assign(packed.data() + offset, (size_t)length);
		offset += length;
	}
	if(offset != packed.size()) throw glare::Exception("Compressed scientific object tables contain trailing data.");
}
}
