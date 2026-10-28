#include "../acquisition/LslRecordingFile.h"

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

float floatFromBits(std::uint32_t bits)
{
	float value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

std::uint64_t bitsOf(double value)
{
	std::uint64_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

bool sameFloat(float a, float b)
{
	return std::memcmp(&a, &b, sizeof(a)) == 0;
}

neurocontrol::EegFrame frame(std::uint32_t n)
{
	neurocontrol::EegFrame value;
	value.raw_source_values_exact = true;
	for(std::uint32_t channel = 0; channel < 20; ++channel)
		value.raw_source_values.push_back(floatFromBits(0x3f000000u + n * 20u + channel));
	if(n == 0) value.raw_source_values[0] = floatFromBits(0x80000000u); // negative zero
	if(n == 1) value.raw_source_values[4] = floatFromBits(0x7fc01234u); // NaN payload
	value.source_timestamp_seconds = n < 3 ? 1790000000.0 : 1790000001.0;
	value.arrival_monotonic_seconds = n < 2 ? 23.5 : 23.7;
	return value;
}

int fail(const char* message)
{
	std::cerr << message << '\n';
	return 1;
}

} // namespace

int main()
{
	const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto folder = std::filesystem::temp_directory_path() / ("metasiberia_lsl_roundtrip_" + std::to_string(nonce));
	std::error_code ec;
	std::filesystem::create_directories(folder, ec);
	if(ec) return fail("could not create temporary directory");
	const auto partial = folder / "known.nlsr.part";
	const auto final = folder / "known.nlsr";
	const auto incomplete = folder / "incomplete.nlsr.part";
	std::string error;

	{
		neurocontrol::LslRecordingWriter writer;
		if(!writer.open(partial, 20, error)) return fail(error.c_str());
		std::vector<neurocontrol::EegFrame> first{frame(0), frame(1)};
		std::vector<neurocontrol::EegFrame> second{frame(2), frame(3)};
		if(!writer.appendChunk(first, error) || !writer.appendChunk(second, error)) return fail(error.c_str());
		if(writer.sampleCount() != 4) return fail("writer sample count mismatch");
		if(!writer.finalize(final, error)) return fail(error.c_str());
	}

	neurocontrol::LslRecordingReader reader;
	neurocontrol::LslRecordingInfo info;
	if(!reader.open(final, info, error)) return fail(error.c_str());
	if(!info.complete || info.channel_count != 20 || info.sample_count != 4) return fail("completed recording header mismatch");
	if(!reader.verify(info, error) || !info.integrity_verified || !reader.info().integrity_verified)
		return fail("verified checksum state was not copied back to the caller's recording info");
	std::vector<neurocontrol::EegFrame> roundtrip;
	if(!reader.readRange(0, info.sample_count, roundtrip, error)) return fail(error.c_str());
	for(std::size_t row = 0; row < roundtrip.size(); ++row)
	{
		const auto expected = frame(static_cast<std::uint32_t>(row));
		if(roundtrip[row].raw_source_values.size() != 20) return fail("roundtrip channel count mismatch");
		for(std::size_t channel = 0; channel < 20; ++channel)
			if(!sameFloat(roundtrip[row].raw_source_values[channel], expected.raw_source_values[channel])) return fail("float32 payload changed during roundtrip");
		if(bitsOf(roundtrip[row].source_timestamp_seconds) != bitsOf(expected.source_timestamp_seconds)) return fail("source timestamp changed during roundtrip");
		if(bitsOf(roundtrip[row].arrival_monotonic_seconds) != bitsOf(expected.arrival_monotonic_seconds)) return fail("arrival timestamp changed during roundtrip");
	}
	if(roundtrip[0].source_timestamp_seconds != roundtrip[1].source_timestamp_seconds ||
		roundtrip[0].arrival_monotonic_seconds != roundtrip[1].arrival_monotonic_seconds ||
		roundtrip[1].arrival_monotonic_seconds == roundtrip[2].arrival_monotonic_seconds)
		return fail("repeated timestamps were not preserved");

	const auto corrupted = folder / "corrupted.nlsr";
	std::filesystem::copy_file(final, corrupted);
	{
		std::fstream file(corrupted, std::ios::binary | std::ios::in | std::ios::out);
		file.seekg(48, std::ios::beg); // First payload byte follows the fixed v1 header.
		char byte = 0;
		file.read(&byte, 1);
		byte ^= 0x01;
		file.seekp(48, std::ios::beg);
		file.write(&byte, 1);
		if(!file) return fail("could not create checksum-mismatch fixture");
	}
	neurocontrol::LslRecordingReader corrupt_reader;
	if(!corrupt_reader.open(corrupted, info, error)) return fail(error.c_str());
	if(corrupt_reader.verify(info, error) || info.integrity_verified || error.find("checksum mismatch") == std::string::npos)
		return fail("payload corruption was not reported as an integrity failure with a reason");

	{
		neurocontrol::LslRecordingWriter writer;
		if(!writer.open(incomplete, 20, error)) return fail(error.c_str());
		std::vector<neurocontrol::EegFrame> samples{frame(0), frame(1), frame(2)};
		if(!writer.appendChunk(samples, error)) return fail(error.c_str());
		// Destruction intentionally leaves a recoverable .part file.
	}
	neurocontrol::LslRecordingReader incomplete_reader;
	if(!incomplete_reader.open(incomplete, info, error)) return fail(error.c_str());
	if(info.complete || info.integrity_verified || info.sample_count != 3) return fail("incomplete recording was not identified");
	std::filesystem::remove_all(folder, ec);
	std::cout << "LSL recording round-trip OK: 20 channels, float32 bits, repeated source/arrival timestamps, incomplete marker\n";
	return 0;
}
