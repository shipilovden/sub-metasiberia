#pragma once

#include "EegSource.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace neurocontrol {

// Lossless container for numeric LSL float32 samples. Each sample stores the
// source timestamp and the receiver's monotonic timestamp (one per LSL chunk,
// repeated for samples returned in that chunk). The JSON sidecar is managed by
// the Qt client and preserves stream XML and channel metadata.
class LslRecordingWriter final {
public:
	LslRecordingWriter() = default;
	~LslRecordingWriter();
	LslRecordingWriter(const LslRecordingWriter&) = delete;
	LslRecordingWriter& operator=(const LslRecordingWriter&) = delete;

	bool open(const std::filesystem::path& partial_path, std::uint32_t channel_count, std::string& error);
	bool appendChunk(const std::vector<EegFrame>& frames, std::string& error);
	bool finalize(const std::filesystem::path& final_path, std::string& error);
	void leaveIncomplete();
	std::uint64_t sampleCount() const { return sample_count_; }
	std::uint64_t checksum() const { return checksum_; }
	std::uint32_t channelCount() const { return channel_count_; }
	bool isOpen() const { return file_.is_open(); }

private:
	bool writeHeader(bool complete, std::string& error);
	std::fstream file_;
	std::filesystem::path partial_path_;
	std::uint32_t channel_count_ = 0;
	std::uint32_t record_size_ = 0;
	std::uint64_t sample_count_ = 0;
	std::uint64_t checksum_ = 14695981039346656037ull;
};

struct LslRecordingInfo {
	std::uint32_t channel_count = 0;
	std::uint32_t record_size = 0;
	std::uint64_t sample_count = 0;
	std::uint64_t checksum = 0;
	bool complete = false;
	bool integrity_verified = false;
	bool trailing_partial_record = false;
};

class LslRecordingReader final {
public:
	bool open(const std::filesystem::path& path, LslRecordingInfo& info, std::string& error);
	bool readRange(std::uint64_t first_sample, std::uint64_t count,
		std::vector<EegFrame>& frames, std::string& error);
	bool verify(std::string& error);
	const LslRecordingInfo& info() const { return info_; }

private:
	std::filesystem::path path_;
	LslRecordingInfo info_;
};

} // namespace neurocontrol
