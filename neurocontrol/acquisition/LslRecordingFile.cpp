#include "LslRecordingFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace neurocontrol {
namespace {

constexpr std::array<char, 8> kMagic{{'M', 'S', 'L', 'S', 'R', '0', '1', '\0'}};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kHeaderSize = 48;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

template<class T>
std::array<unsigned char, sizeof(T)> littleEndian(T value)
{
	std::array<unsigned char, sizeof(T)> bytes{};
	std::memcpy(bytes.data(), &value, sizeof(T));
	const std::uint16_t one = 1;
	if(*reinterpret_cast<const unsigned char*>(&one) != 1)
		for(std::size_t i = 0; i < bytes.size() / 2; ++i)
			std::swap(bytes[i], bytes[bytes.size() - 1 - i]);
	return bytes;
}

template<class T>
T fromLittleEndian(const std::array<unsigned char, sizeof(T)>& input)
{
	auto bytes = input;
	const std::uint16_t one = 1;
	if(*reinterpret_cast<const unsigned char*>(&one) != 1)
		for(std::size_t i = 0; i < bytes.size() / 2; ++i)
			std::swap(bytes[i], bytes[bytes.size() - 1 - i]);
	T value{};
	std::memcpy(&value, bytes.data(), sizeof(T));
	return value;
}

void updateHash(std::uint64_t& hash, const unsigned char* bytes, std::size_t count)
{
	for(std::size_t i = 0; i < count; ++i)
	{
		hash ^= bytes[i];
		hash *= kFnvPrime;
	}
}

template<class T>
bool writeValue(std::ostream& out, T value, std::uint64_t* hash = nullptr)
{
	const auto bytes = littleEndian(value);
	out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if(hash) updateHash(*hash, bytes.data(), bytes.size());
	return static_cast<bool>(out);
}

template<class T>
bool readValue(std::istream& in, T& value)
{
	std::array<unsigned char, sizeof(T)> bytes{};
	in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if(!in) return false;
	value = fromLittleEndian<T>(bytes);
	return true;
}

bool writeHeaderTo(std::iostream& io, std::uint32_t channels, std::uint32_t record_size,
	std::uint64_t sample_count, bool complete, std::uint64_t checksum)
{
	io.seekp(0, std::ios::beg);
	io.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
	if(!writeValue(io, kVersion) || !writeValue(io, kHeaderSize) || !writeValue(io, channels) ||
		!writeValue(io, record_size) || !writeValue(io, sample_count) ||
		!writeValue(io, static_cast<std::uint32_t>(complete ? 1 : 0)) || !writeValue(io, std::uint32_t{0}) ||
		!writeValue(io, checksum)) return false;
	io.flush();
	return static_cast<bool>(io);
}

} // namespace

LslRecordingWriter::~LslRecordingWriter()
{
	if(file_.is_open()) leaveIncomplete();
}

bool LslRecordingWriter::open(const std::filesystem::path& partial_path, std::uint32_t channel_count, std::string& error)
{
	if(file_.is_open()) { error = "LSL recording writer is already open."; return false; }
	if(channel_count == 0 || channel_count > 256) { error = "LSL recording channel count is outside 1..256."; return false; }
	std::error_code ec;
	if(std::filesystem::exists(partial_path, ec) || ec) { error = "LSL recording output already exists or cannot be checked."; return false; }
	const std::uint64_t record_size = static_cast<std::uint64_t>(channel_count) * sizeof(float) + 2 * sizeof(double);
	if(record_size > std::numeric_limits<std::uint32_t>::max()) { error = "LSL recording record size overflow."; return false; }
	partial_path_ = partial_path;
	channel_count_ = channel_count;
	record_size_ = static_cast<std::uint32_t>(record_size);
	sample_count_ = 0;
	checksum_ = kFnvOffset;
	file_.open(partial_path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
	if(!file_) { error = "Cannot create LSL recording file."; return false; }
	if(!writeHeader(false, error)) { leaveIncomplete(); return false; }
	file_.seekp(kHeaderSize, std::ios::beg);
	return true;
}

bool LslRecordingWriter::writeHeader(bool complete, std::string& error)
{
	if(!file_.is_open()) { error = "LSL recording file is not open."; return false; }
	if(!writeHeaderTo(file_, channel_count_, record_size_, sample_count_, complete, checksum_))
	{
		error = "Cannot update LSL recording header.";
		return false;
	}
	file_.seekp(0, std::ios::end);
	return true;
}

bool LslRecordingWriter::appendChunk(const std::vector<EegFrame>& frames, std::string& error)
{
	if(!file_.is_open()) { error = "LSL recording is not active."; return false; }
	if(frames.empty()) return true;
	std::uint64_t next_hash = checksum_;
	std::uint64_t written = 0;
	for(const EegFrame& frame : frames)
	{
		if(frame.raw_source_values.size() != channel_count_ || !frame.raw_source_values_exact)
		{
			error = "LSL recording requires exact native float32 samples with the configured channel count.";
			return false;
		}
		for(float value : frame.raw_source_values)
			if(!writeValue(file_, value, &next_hash)) { error = "Failed writing LSL float32 samples."; return false; }
		if(!writeValue(file_, frame.source_timestamp_seconds, &next_hash) ||
			!writeValue(file_, frame.arrival_monotonic_seconds, &next_hash))
		{
			error = "Failed writing LSL timestamps.";
			return false;
		}
		++written;
	}
	file_.flush();
	if(!file_) { error = "Failed flushing LSL recording chunk."; return false; }
	if(written > std::numeric_limits<std::uint64_t>::max() - sample_count_)
		{ error = "LSL recording sample count overflow."; return false; }
	sample_count_ += written;
	checksum_ = next_hash;
	return writeHeader(false, error);
}

bool LslRecordingWriter::finalize(const std::filesystem::path& final_path, std::string& error)
{
	if(!file_.is_open()) { error = "LSL recording file is not open."; return false; }
	if(!writeHeader(true, error)) { leaveIncomplete(); return false; }
	file_.close();
	if(file_.fail()) { error = "Failed closing LSL recording file."; leaveIncomplete(); return false; }
	std::error_code ec;
	if(std::filesystem::exists(final_path, ec) || ec)
	{
		error = "Final LSL recording path already exists or cannot be checked.";
		return false;
	}
	std::filesystem::rename(partial_path_, final_path, ec);
	if(ec) { error = "Cannot finalize LSL recording: " + ec.message(); leaveIncomplete(); return false; }
	partial_path_ = final_path;
	return true;
}

void LslRecordingWriter::leaveIncomplete()
{
	if(!file_.is_open())
	{
		if(partial_path_.empty()) return;
		file_.clear();
		file_.open(partial_path_, std::ios::binary | std::ios::in | std::ios::out);
		if(!file_) return;
	}
	file_.clear();
	std::string ignored;
	writeHeader(false, ignored);
	file_.flush();
	file_.close();
}

bool LslRecordingReader::open(const std::filesystem::path& path, LslRecordingInfo& info, std::string& error)
{
	path_ = path;
	info_ = {};
	std::ifstream in(path_, std::ios::binary);
	if(!in) { error = "Cannot open LSL recording file."; return false; }
	std::array<char, kMagic.size()> magic{};
	in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
	std::uint32_t version = 0, header_size = 0, flags = 0, reserved = 0;
	std::uint64_t sample_count = 0;
	if(!in || magic != kMagic || !readValue(in, version) || !readValue(in, header_size) ||
		!readValue(in, info_.channel_count) || !readValue(in, info_.record_size) ||
		!readValue(in, sample_count) || !readValue(in, flags) || !readValue(in, reserved) || !readValue(in, info_.checksum))
	{
		error = "Invalid or truncated LSL recording header.";
		return false;
	}
	const std::uint64_t expected_record_size = static_cast<std::uint64_t>(info_.channel_count) * sizeof(float) + 2 * sizeof(double);
	if(version != kVersion || header_size != kHeaderSize || info_.channel_count == 0 || info_.channel_count > 256 ||
		info_.record_size != expected_record_size || flags > 1)
	{
		error = "Unsupported or invalid LSL recording header.";
		return false;
	}
	std::error_code ec;
	const std::uint64_t file_size = std::filesystem::file_size(path_, ec);
	if(ec || file_size < kHeaderSize) { error = "Cannot determine LSL recording file size."; return false; }
	const std::uint64_t payload_size = file_size - kHeaderSize;
	const std::uint64_t complete_records = payload_size / info_.record_size;
	info_.trailing_partial_record = payload_size % info_.record_size != 0;
	info_.complete = flags == 1 && !info_.trailing_partial_record && complete_records == sample_count;
	if(flags == 1 && !info_.complete) { error = "Finalized LSL recording has an inconsistent sample count or truncated record."; return false; }
	info_.sample_count = info_.complete ? sample_count : complete_records;
	info = info_;
	return true;
}

bool LslRecordingReader::readRange(std::uint64_t first_sample, std::uint64_t count,
	std::vector<EegFrame>& frames, std::string& error)
{
	frames.clear();
	if(first_sample > info_.sample_count || count > info_.sample_count - first_sample)
		{ error = "Requested LSL recording range is outside the file."; return false; }
	if(count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
		{ error = "Requested LSL recording range is too large."; return false; }
	std::ifstream in(path_, std::ios::binary);
	if(!in) { error = "Cannot reopen LSL recording file."; return false; }
	const std::uint64_t offset = kHeaderSize + first_sample * info_.record_size;
	if(offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
		{ error = "LSL recording offset exceeds stream limits."; return false; }
	in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
	frames.reserve(static_cast<std::size_t>(count));
	for(std::uint64_t i = 0; i < count; ++i)
	{
		EegFrame frame;
		frame.raw_source_values.resize(info_.channel_count);
		frame.raw_source_values_exact = true;
		for(float& value : frame.raw_source_values)
			if(!readValue(in, value)) { error = "Truncated LSL sample values."; frames.clear(); return false; }
		if(!readValue(in, frame.source_timestamp_seconds) || !readValue(in, frame.arrival_monotonic_seconds))
			{ error = "Truncated LSL sample timestamps."; frames.clear(); return false; }
		frames.push_back(std::move(frame));
	}
	return true;
}

bool LslRecordingReader::verify(std::string& error)
{
	if(!info_.complete) { error = "Incomplete LSL recording cannot be marked verified."; return false; }
	std::ifstream in(path_, std::ios::binary);
	if(!in) { error = "Cannot read LSL recording for integrity verification."; return false; }
	in.seekg(kHeaderSize, std::ios::beg);
	std::array<unsigned char, 65536> buffer{};
	std::uint64_t left = info_.sample_count * info_.record_size;
	std::uint64_t hash = kFnvOffset;
	while(left > 0)
	{
		const auto request = static_cast<std::streamsize>(std::min<std::uint64_t>(left, buffer.size()));
		in.read(reinterpret_cast<char*>(buffer.data()), request);
		if(in.gcount() != request) { error = "Truncated LSL recording payload."; return false; }
		updateHash(hash, buffer.data(), static_cast<std::size_t>(request));
		left -= static_cast<std::uint64_t>(request);
	}
	if(hash != info_.checksum) { error = "LSL recording checksum mismatch."; return false; }
	info_.integrity_verified = true;
	return true;
}

bool LslRecordingReader::verify(LslRecordingInfo& info, std::string& error)
{
	const bool verified = verify(error);
	info = info_;
	return verified;
}

} // namespace neurocontrol
