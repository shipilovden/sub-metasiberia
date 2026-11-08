/*=====================================================================
GameStateDelta.h
----------------
Small prefix/suffix deltas for reliable scripted-game state snapshots.
=====================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace GameStateDelta
{
static const size_t MAX_STATE_BYTES = 16 * 1024;

struct Delta
{
	uint32_t base_version = 0;
	uint32_t result_size = 0;
	uint32_t prefix_size = 0;
	uint32_t suffix_size = 0;
	std::vector<unsigned char> replacement;
};

inline bool create(const std::vector<unsigned char>& previous, const std::vector<unsigned char>& current,
	uint32_t base_version, Delta& delta_out)
{
	if(previous.empty() || current.empty() || previous.size() > MAX_STATE_BYTES || current.size() > MAX_STATE_BYTES || base_version == 0)
		return false;
	size_t prefix = 0;
	while(prefix < previous.size() && prefix < current.size() && previous[prefix] == current[prefix]) ++prefix;
	size_t suffix = 0;
	while(suffix < previous.size() - prefix && suffix < current.size() - prefix &&
		previous[previous.size() - suffix - 1] == current[current.size() - suffix - 1]) ++suffix;
	const size_t replacement_size = current.size() - prefix - suffix;
	// The wire representation adds four uint32 fields and a string length.
	if(replacement_size + 20 >= current.size()) return false;
	delta_out.base_version = base_version;
	delta_out.result_size = (uint32_t)current.size();
	delta_out.prefix_size = (uint32_t)prefix;
	delta_out.suffix_size = (uint32_t)suffix;
	delta_out.replacement.assign(current.begin() + prefix, current.begin() + prefix + replacement_size);
	return true;
}

inline bool apply(const std::vector<unsigned char>& previous, uint32_t previous_version, const Delta& delta,
	std::vector<unsigned char>& result_out)
{
	if(previous.empty() || previous.size() > MAX_STATE_BYTES || delta.base_version != previous_version ||
	delta.result_size == 0 || delta.result_size > MAX_STATE_BYTES ||
	delta.prefix_size > previous.size() || delta.suffix_size > previous.size() - delta.prefix_size ||
	delta.prefix_size + delta.suffix_size + delta.replacement.size() != delta.result_size)
		return false;
	result_out.clear();
	result_out.reserve(delta.result_size);
	result_out.insert(result_out.end(), previous.begin(), previous.begin() + delta.prefix_size);
	result_out.insert(result_out.end(), delta.replacement.begin(), delta.replacement.end());
	result_out.insert(result_out.end(), previous.end() - delta.suffix_size, previous.end());
	return result_out.size() == delta.result_size;
}
}
