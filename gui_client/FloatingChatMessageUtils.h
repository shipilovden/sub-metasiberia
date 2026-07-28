/*=====================================================================
FloatingChatMessageUtils.h
--------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "EmojiUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/UTF8Utils.h"
#include <cassert>
#include <string>


namespace FloatingChatMessageUtils
{
inline size_t maxPreviewCodePointCount()
{
	return 28;
}


inline std::string collapseWhitespace(const std::string& text)
{
	std::string out;
	out.reserve(text.size());

	bool prev_was_space = false;
	for(size_t i=0; i<text.size(); ++i)
	{
		const char c = text[i];
		const bool is_space = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
		if(is_space)
		{
			if(!prev_was_space)
			{
				out.push_back(' ');
				prev_was_space = true;
			}
		}
		else
		{
			out.push_back(c);
			prev_was_space = false;
		}
	}

	return out;
}


inline std::string makePreview(const std::string& raw_message)
{
	std::string preview = UTF8Utils::sanitiseUTF8String(raw_message);
	preview = collapseWhitespace(preview);
	preview = ::stripHeadAndTailWhitespace(preview);
	if(preview.empty())
		return std::string();

	if(EmojiUtils::isSupportedEmoji(preview))
		return preview;

	const size_t num_code_points = UTF8Utils::numCodePointsInString(preview);
	if(num_code_points <= maxPreviewCodePointCount())
		return preview;

	const size_t end_byte = UTF8Utils::byteIndex((const uint8*)preview.data(), preview.size(), maxPreviewCodePointCount());
	return preview.substr(0, end_byte) + "...";
}


inline void test()
{
	assert(collapseWhitespace("hello") == "hello");
	assert(collapseWhitespace("hello   world") == "hello world");
	assert(collapseWhitespace("hello\r\n\tworld") == "hello world");

	assert(makePreview("  hello  ") == "hello");
	assert(makePreview("hello\nworld") == "hello world");
	assert(makePreview(" \t\r\n ") == "");

	const std::string emoji(EmojiUtils::supportedEmoji()[0]);
	assert(makePreview(emoji) == emoji);

	const std::string long_text = "1234567890123456789012345678901234567890";
	const std::string preview = makePreview(long_text);
	assert(preview.size() >= 3);
	assert(preview.substr(preview.size() - 3) == "...");
}
}
