// Standalone C++17 test: only the catalog header and the standard library.
#include "../../gui_client/EmojiUtils.h"

#include <iostream>
#include <set>
#include <stdexcept>

namespace
{
void check(bool condition, const char* message)
{
	if(!condition)
		throw std::runtime_error(message);
}

bool contains(const std::vector<std::string>& results, std::string_view emoji)
{
	return std::find(results.begin(), results.end(), emoji) != results.end();
}

void checkResults(const std::vector<std::string>& results)
{
	check(results.size() <= EmojiUtils::supportedEmoji().size(), "results exceed catalog");
	std::set<std::string> seen;
	for(const auto& emoji : results)
	{
		check(EmojiUtils::isSupportedEmoji(emoji), "unsupported search result");
		check(seen.insert(emoji).second, "duplicate search result");
	}
}

void checkMatch(const std::string& query, std::string_view expected)
{
	const auto results = EmojiUtils::searchEmoji(query);
	checkResults(results);
	check(contains(results, expected), "expected search match missing");
}
}

int main()
{
	try
	{
		EmojiUtils::test();
		const auto& categories = EmojiUtils::emojiCategories();
		check(categories.size() >= 8, "missing categories");
		const std::string_view original_titles[] = {
			"\xD0\x9B\xD0\xB8\xD1\x86\xD0\xB0", "\xD0\xAD\xD0\xBC\xD0\xBE\xD1\x86\xD0\xB8\xD0\xB8", "\xD0\x96\xD0\xB5\xD1\x81\xD1\x82\xD1\x8B", "\xD0\xAD\xD1\x84\xD1\x84\xD0\xB5\xD0\xBA\xD1\x82\xD1\x8B", "\xD0\x96\xD0\xB8\xD0\xB2\xD0\xBE\xD1\x82\xD0\xBD\xD1\x8B\xD0\xB5", "\xD0\xA1\xD0\xB5\xD1\x80\xD0\xB4\xD1\x86\xD0\xB0"
		};
		for(size_t i=0; i<6; ++i)
			check(categories[i].title == original_titles[i], "original category order changed");
		check(categories[6].title == "\xD0\x95\xD0\xB4\xD0\xB0\x20\xD0\xB8\x20\xD0\xBD\xD0\xB0\xD0\xBF\xD0\xB8\xD1\x82\xD0\xBA\xD0\xB8", "food category missing");
		check(categories[7].title == "\xD0\x97\xD0\xB0\xD0\xBD\xD1\x8F\xD1\x82\xD0\xB8\xD1\x8F\x20\xD0\xB8\x20\xD0\xBF\xD1\x83\xD1\x82\xD0\xB5\xD1\x88\xD0\xB5\xD1\x81\xD1\x82\xD0\xB2\xD0\xB8\xD1\x8F", "activity category missing");
		const size_t added_count = categories[6].emojis.size() + categories[7].emojis.size();
		check(added_count >= 40 && added_count <= 60, "unexpected extension size");

		std::set<std::string> old_emojis;
		for(size_t i=0; i<6; ++i)
			for(const auto& definition : categories[i].emojis)
				old_emojis.insert(std::string(definition.emoji));
		std::set<std::string> additions;
		for(size_t i=6; i<8; ++i)
		{
			check(categories[i].num_columns > 0, "invalid category layout");
			for(const auto& definition : categories[i].emojis)
			{
				const std::string emoji(definition.emoji);
				check(old_emojis.count(emoji) == 0, "addition already in original catalog");
				check(additions.insert(emoji).second, "duplicate addition");
				check(!definition.name.empty() && !definition.aliases.empty(), "missing search metadata");
				// Count UTF-8 leading bytes independently of the search decoder.
				size_t codepoints = 0;
				for(unsigned char byte : emoji)
					if((byte & 0xC0) != 0x80) ++codepoints;
				check(codepoints == 1, "new emoji must be a single codepoint");
				check(emoji.find("\xE2\x80\x8D") == std::string::npos, "ZWJ addition");
				check(emoji.find("\xEF\xB8\x8F") == std::string::npos, "variation-selector addition");
				check(emoji.size() != 4 || emoji.compare(0, 3, "\xF0\x9F\x87") != 0, "flag addition");
				checkMatch(emoji, emoji);
				checkMatch(std::string(definition.name), emoji);
				checkMatch(std::string(definition.aliases), emoji);
			}
		}

		std::vector<std::string> expected_order;
		for(const auto emoji : EmojiUtils::supportedEmoji())
			if(!contains(expected_order, emoji)) expected_order.push_back(std::string(emoji));
		const auto all = EmojiUtils::searchEmoji("");
		checkResults(all);
		check(all == expected_order, "empty search must retain unique catalog order");
		check(EmojiUtils::searchEmoji(" \t\r\n") == all, "whitespace search differs");
		for(const auto& category : categories)
		{
			const auto results = EmojiUtils::searchEmoji(std::string(category.title));
			checkResults(results);
			for(const auto& definition : category.emojis)
			{
				check(contains(results, definition.emoji), "category search omitted emoji");
				checkMatch(std::string(definition.name), definition.emoji);
			}
		}

		checkMatch("\xD0\xA3\xD0\x9B\xD0\xAB\xD0\x91\xD0\x9A\xD0\x90", "\xF0\x9F\x98\x80");
		checkMatch("\xD0\xA1\xD0\x95\xD0\xA0\xD0\x94\xD0\xA6\xD0\x95", "\xE2\x9D\xA4\xEF\xB8\x8F");
		checkMatch("\xD0\x9E\xD0\x93\xD0\x9E\xD0\x9D\xD0\xAC", "\xF0\x9F\x94\xA5");
		checkMatch("\xD0\x9B\xD0\x90\xD0\x99\xD0\x9A", "\xF0\x9F\x91\x8D");
		checkMatch("\xD0\x9F\xD0\x90\xD0\x9B\xD0\x95\xD0\xA6\x20\xD0\x92\xD0\x92\xD0\x95\xD0\xA0\xD0\xA5", "\xF0\x9F\x91\x8D");
		checkMatch("\xD0\xA1\xD0\x9B\xD0\x81\xD0\x97\xD0\xAB", "\xF0\x9F\x98\x82");
		checkMatch("\xD0\xA1\xD0\x9B\xD0\x95\xD0\x97\xD0\xAB", "\xF0\x9F\x98\x82");
		checkMatch("\xD0\x96\xD0\x95\xD0\xA1\xD0\xA2\xD0\xAB", "\xF0\x9F\x91\x8B");
		checkMatch("\xD0\x95\xD0\x94\xD0\x90\x20\xD0\x98\x20\xD0\x9D\xD0\x90\xD0\x9F\xD0\x98\xD0\xA2\xD0\x9A\xD0\x98", "\xF0\x9F\x8D\x95");
		checkMatch("\xD0\x9F\xD0\xA3\xD0\xA2\xD0\x95\xD0\xA8\xD0\x95\xD0\xA1\xD0\xA2\xD0\x92\xD0\x98\xD0\xAF", "\xF0\x9F\x9A\xB2");
		checkMatch("\xD0\x9A\xD0\x9E\xD0\xA4\xD0\x95", "\xE2\x98\x95");
		checkMatch("\xD0\xA4\xD0\xA3\xD0\xA2\xD0\x91\xD0\x9E\xD0\x9B", "\xE2\x9A\xBD");
		checkMatch("THUMBUP", "\xF0\x9F\x91\x8D");
		checkMatch("SMILE", "\xF0\x9F\x98\x80");
		checkMatch("  FiRe\t", "\xF0\x9F\x94\xA5");
		checkMatch("OK HAND", "\xF0\x9F\x91\x8C");
		check(EmojiUtils::searchEmoji("HEART") == EmojiUtils::searchEmoji("heart"), "ASCII case folding");
		check(EmojiUtils::searchEmoji("\xD0\xA1\xD0\x9B\xD0\x81\xD0\x97\xD0\xAB") == EmojiUtils::searchEmoji("\xD1\x81\xD0\xBB\xD1\x91\xD0\xB7\xD1\x8B"), "Russian Yo case folding");
		check(EmojiUtils::searchEmoji("\xD0\xA3\xD0\x9B\xD0\xAB\xD0\x91\xD0\x9A\xD0\x90") == EmojiUtils::searchEmoji("\xD1\x83\xD0\xBB\xD1\x8B\xD0\xB1\xD0\xBA\xD0\xB0"), "Russian case folding");
		check(EmojiUtils::searchEmoji("\xF0\x9F\x98\x8D") == std::vector<std::string>{"\xF0\x9F\x98\x8D"}, "shared emoji not deduplicated");
		const auto shared_text = EmojiUtils::emojiSearchText("\xF0\x9F\x98\x8D");
		check(shared_text.find("\xD0\x9B\xD0\xB8\xD1\x86\xD0\xB0") != std::string::npos &&
			shared_text.find("\xD0\xA1\xD0\xB5\xD1\x80\xD0\xB4\xD1\x86\xD0\xB0") != std::string::npos &&
			shared_text.find("smiling face with heart-eyes") != std::string::npos, "shared metadata lost");
		check(EmojiUtils::emojiSearchText("unsupported").empty(), "unsupported metadata");

		const std::string unsupported[] = {
			"not-in-catalog", "\xD0\xBD\xD0\xB5\xD1\x81\xD1\x83\xD1\x89\xD0\xB5\xD1\x81\xD1\x82\xD0\xB2\xD1\x83\xD1\x8E\xD1\x89\xD0\xB8\xD0\xB9\xD0\xB7\xD0\xB0\xD0\xBF\xD1\x80\xD0\xBE\xD1\x81", "\xE4\xB8\xAD\xE6\x96\x87", "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB", "\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA",
			std::string(4096, 'z'), std::string(1, '\0'),
			"\x80", "\xD0", "\xD0!", "\xC0\xAF", "\xE0\x80\xAF",
			"\xED\xA0\x80", "\xF0\x80\x80\xAF", "\xF4\x90\x80\x80", "\xFF",
			std::string("fire") + "\xD0"
		};
		for(const auto& query : unsupported)
			check(EmojiUtils::searchEmoji(query).empty(), "unsupported or malformed query matched");

		const auto picker = EmojiUtils::buildPickerCategories({"\xF0\x9F\x8D\x95", "\xF0\x9F\x8D\x95", "unsupported", "\xE2\x98\x95"});
		check(picker.size() == categories.size() + 1, "picker lost a category");
		check(picker.front().emojis == std::vector<std::string>({"\xF0\x9F\x8D\x95", "\xE2\x98\x95"}), "recent filtering changed");
		const auto capped = EmojiUtils::buildPickerCategories(all);
		check(capped.front().emojis.size() == EmojiUtils::maxRecentEmojiCount(), "recent limit changed");
		std::cout << "Emoji catalog checks passed: " << all.size() << " unique emoji, "
			<< added_count << " additions.\n";
		return 0;
	}
	catch(const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}

