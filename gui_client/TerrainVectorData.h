#pragma once

// Original OSM segments travel with the existing PNG mask resource. msVG is a
// private, ancillary, unsafe-to-copy PNG chunk: image editors must drop it when
// modifying the pixels. Coordinates use the mask's north-to-south row order.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace TerrainVectorData
{
struct Segment { float ax, ay, bx, by, width; uint32_t style; };
struct Data { std::vector<Segment> segments; };
constexpr size_t MAX_SEGMENTS = 500000;
constexpr size_t MAX_PNG_BYTES = 32 * 1024 * 1024;

namespace Detail
{
inline uint32_t readBE(const uint8_t* p) { return (uint32_t(p[0])<<24) | (uint32_t(p[1])<<16) | (uint32_t(p[2])<<8) | p[3]; }
inline uint32_t readLE(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); }
inline void appendBE(std::vector<uint8_t>& out, uint32_t n) { for(int i=3;i>=0;--i) out.push_back(uint8_t(n>>(i*8))); }
inline void appendLE(std::vector<uint8_t>& out, uint32_t n) { for(int i=0;i<4;++i) out.push_back(uint8_t(n>>(i*8))); }
inline float readFloat(const uint8_t* p) { const uint32_t bits=readLE(p); float f; std::memcpy(&f,&bits,4); return f; }
inline void appendFloat(std::vector<uint8_t>& out, float f) { uint32_t bits; std::memcpy(&bits,&f,4); appendLE(out,bits); }
inline uint32_t crc32(const uint8_t* p, size_t n)
{
	static const std::vector<uint32_t> table = []() {
		std::vector<uint32_t> t(256);
		for(uint32_t i=0;i<256;++i) { uint32_t c=i; for(int j=0;j<8;++j) c=(c>>1)^((c&1)?0xedb88320u:0u); t[i]=c; }
		return t;
	}();
	uint32_t crc=0xffffffffu;
	for(size_t i=0;i<n;++i) crc=table[(crc^p[i])&255]^(crc>>8);
	return crc^0xffffffffu;
}
inline bool valid(const Segment& s)
{
	const float coords[]={s.ax,s.ay,s.bx,s.by};
	for(float v:coords) if(!std::isfinite(v) || v<0.f || v>1.f) return false;
	return std::isfinite(s.width) && s.width>0.f && s.width<=1.f && s.style<=3;
}
// Validate lengths before touching chunk contents, including the terminating
// IEND and all CRCs. A malformed optional vector chunk never reaches rendering.
inline bool inspectPNG(const uint8_t* p, size_t n, size_t& iend, size_t& vector_chunk)
{
	static const uint8_t signature[]={137,80,78,71,13,10,26,10};
	if(!p || n<45 || n>MAX_PNG_BYTES || std::memcmp(p,signature,8)!=0) return false;
	vector_chunk=0;
	bool image_data=false;
	for(size_t pos=8; pos<=n-12;)
	{
		const size_t len=readBE(p+pos);
		if(len>n-pos-12) return false;
		const uint8_t* type=p+pos+4;
		if(pos==8 && (std::memcmp(type,"IHDR",4)!=0 || len!=13)) return false;
		if(pos!=8 && std::memcmp(type,"IHDR",4)==0) return false;
		if(crc32(type,len+4)!=readBE(p+pos+8+len)) return false;
		if(std::memcmp(type,"IDAT",4)==0) image_data=true;
		if(std::memcmp(type,"msVG",4)==0) { if(vector_chunk) return false; vector_chunk=pos; }
		if(std::memcmp(type,"IEND",4)==0) { iend=pos; return image_data && len==0 && pos+12==n; }
		pos+=len+12;
	}
	return false;
}
}

// Payload: LE uint32 version=1, count; count records of five IEEE754 float32
// values and a uint32 style. nullptr means absent/invalid; empty Data is valid.
inline std::shared_ptr<const Data> readPNG(const uint8_t* png, size_t size)
{
	size_t iend=0, offset=0;
	if(!Detail::inspectPNG(png,size,iend,offset) || offset==0) return nullptr;
	const size_t len=Detail::readBE(png+offset);
	const uint8_t* p=png+offset+8;
	if(len<8 || Detail::readLE(p)!=1) return nullptr;
	const size_t count=Detail::readLE(p+4);
	if(count>MAX_SEGMENTS || len!=8+count*24) return nullptr;
	auto result=std::make_shared<Data>();
	result->segments.reserve(count);
	for(size_t i=0;i<count;++i)
	{
		const uint8_t* v=p+8+i*24;
		Segment s{Detail::readFloat(v),Detail::readFloat(v+4),Detail::readFloat(v+8),Detail::readFloat(v+12),Detail::readFloat(v+16),Detail::readLE(v+20)};
		if(!Detail::valid(s)) return nullptr;
		result->segments.push_back(s);
	}
	return result;
}

inline std::vector<uint8_t> writePNG(const uint8_t* png, size_t size, const std::vector<Segment>& segments)
{
	size_t iend=0, previous=0;
	if(!Detail::inspectPNG(png,size,iend,previous)) throw std::runtime_error("Invalid terrain vector PNG");
	if(segments.size()>MAX_SEGMENTS) throw std::runtime_error("Too many terrain vector segments");
	const size_t old_size=previous ? Detail::readBE(png+previous)+12 : 0;
	const size_t payload_size=8+segments.size()*24;
	const size_t output_size=size-old_size+payload_size+12;
	if(output_size>MAX_PNG_BYTES) throw std::runtime_error("Terrain vector PNG exceeds 32 MiB");
	for(const Segment& s:segments) if(!Detail::valid(s)) throw std::runtime_error("Invalid terrain vector coordinates");
	std::vector<uint8_t> out;
	out.reserve(output_size);
	if(previous) { out.insert(out.end(),png,png+previous); out.insert(out.end(),png+previous+old_size,png+iend); }
	else out.insert(out.end(),png,png+iend);
	Detail::appendBE(out,uint32_t(payload_size));
	const size_t crc_start=out.size();
	out.insert(out.end(),{'m','s','V','G'});
	Detail::appendLE(out,1); Detail::appendLE(out,uint32_t(segments.size()));
	for(const Segment& s:segments)
	{
		Detail::appendFloat(out,s.ax); Detail::appendFloat(out,s.ay); Detail::appendFloat(out,s.bx); Detail::appendFloat(out,s.by);
		Detail::appendFloat(out,s.width); Detail::appendLE(out,s.style);
	}
	Detail::appendBE(out,Detail::crc32(out.data()+crc_start,payload_size+4));
	out.insert(out.end(),png+iend,png+size);
	return out;
}
}
