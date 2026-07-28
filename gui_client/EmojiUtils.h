#pragma once


#include <cassert>
#include <string>
#include <string_view>
#include <vector>


namespace EmojiUtils
{

struct EmojiDefinition
{
	std::string_view emoji;
	std::string_view name;
};


struct EmojiCategory
{
	std::string_view title;
	std::vector<EmojiDefinition> emojis;
	int num_columns;
};


struct EmojiPickerCategory
{
	std::string title;
	std::vector<std::string> emojis;
	int num_columns;
};


inline bool isSupportedEmoji(std::string_view text);


inline std::string_view recentCategoryTitle()
{
	return "\xD0\x9F\xD0\xBE\xD1\x81\xD0\xBB\xD0\xB5\xD0\xB4\xD0\xBD\xD0\xB8\xD0\xB5";
}


inline size_t maxRecentEmojiCount()
{
	return 28;
}


inline int recentEmojiColumns()
{
	return 7;
}


// Keep this list to render-safe entries: single codepoint emoji and simple variation-selector forms.
// The current GL text renderer does not shape complex ZWJ sequences reliably, which leads to broken tiles.
inline const std::vector<EmojiCategory>& emojiCategories()
{
	static const std::vector<EmojiCategory> categories = {
		{
			"\xD0\x9B\xD0\xB8\xD1\x86\xD0\xB0",
			{
				{"\xF0\x9F\x98\x80", "grinning face"},
				{"\xF0\x9F\x98\x81", "beaming face"},
				{"\xF0\x9F\x98\x82", "tears of joy"},
				{"\xF0\x9F\x98\x83", "smiling face"},
				{"\xF0\x9F\x98\x84", "smiling face with open mouth"},
				{"\xF0\x9F\x98\x85", "smiling face with sweat"},
				{"\xF0\x9F\x98\x86", "grinning squinting face"},
				{"\xF0\x9F\x98\x89", "winking face"},
				{"\xF0\x9F\x98\x8A", "smiling face with smiling eyes"},
				{"\xF0\x9F\x98\x87", "smiling face with halo"},
				{"\xF0\x9F\xA5\xB0", "smiling face with hearts"},
				{"\xF0\x9F\x98\x8B", "face savoring food"},
				{"\xF0\x9F\x98\x8E", "smiling face with sunglasses"},
				{"\xF0\x9F\x98\x8D", "heart eyes"},
				{"\xF0\x9F\x98\x98", "face blowing a kiss"},
				{"\xF0\x9F\x98\x97", "kissing face"},
				{"\xF0\x9F\x98\x99", "kissing face with smiling eyes"},
				{"\xF0\x9F\x98\x9A", "kissing face with closed eyes"},
				{"\xF0\x9F\x99\x82", "slightly smiling face"},
				{"\xF0\x9F\xA4\x97", "hugging face"},
				{"\xF0\x9F\xA4\xA9", "star-struck"},
				{"\xF0\x9F\xA5\xB3", "partying face"},
				{"\xF0\x9F\xA4\xA0", "cowboy hat face"},
				{"\xF0\x9F\x98\x8F", "smirking face"},
			},
			7
		},
		{
			"\xD0\xAD\xD0\xBC\xD0\xBE\xD1\x86\xD0\xB8\xD0\xB8",
			{
				{"\xF0\x9F\xA4\x94", "thinking face"},
				{"\xF0\x9F\xA4\xA8", "face with raised eyebrow"},
				{"\xF0\x9F\x98\x90", "neutral face"},
				{"\xF0\x9F\x98\x91", "expressionless face"},
				{"\xF0\x9F\x98\xB6", "face without mouth"},
				{"\xF0\x9F\x99\x84", "face with rolling eyes"},
				{"\xF0\x9F\x98\xAC", "grimacing face"},
				{"\xF0\x9F\x98\x8C", "relieved face"},
				{"\xF0\x9F\x98\x94", "pensive face"},
				{"\xF0\x9F\x98\xAA", "sleepy face"},
				{"\xF0\x9F\x98\xB4", "sleeping face"},
				{"\xF0\x9F\x98\xB7", "face with medical mask"},
				{"\xF0\x9F\xA4\x92", "face with thermometer"},
				{"\xF0\x9F\xA4\x95", "face with head bandage"},
				{"\xF0\x9F\xA4\xA2", "nauseated face"},
				{"\xF0\x9F\xA4\xAE", "face vomiting"},
				{"\xF0\x9F\xA4\xA7", "sneezing face"},
				{"\xF0\x9F\x98\xB5", "dizzy face"},
				{"\xF0\x9F\xA4\xAF", "exploding head"},
				{"\xF0\x9F\x98\x95", "confused face"},
				{"\xF0\x9F\x99\x81", "slightly frowning face"},
				{"\xE2\x98\xB9\xEF\xB8\x8F", "frowning face"},
				{"\xF0\x9F\x98\xA3", "persevering face"},
				{"\xF0\x9F\x98\x96", "confounded face"},
				{"\xF0\x9F\x98\xAB", "tired face"},
				{"\xF0\x9F\x98\xA9", "weary face"},
				{"\xF0\x9F\x98\xA2", "crying face"},
				{"\xF0\x9F\x98\xAD", "loudly crying face"},
				{"\xF0\x9F\x98\xA4", "face with steam from nose"},
				{"\xF0\x9F\x98\xA0", "angry face"},
				{"\xF0\x9F\x98\xA1", "pouting face"},
				{"\xF0\x9F\xA4\xAC", "face with symbols on mouth"},
				{"\xF0\x9F\x98\xB3", "flushed face"},
				{"\xF0\x9F\x98\xB1", "face screaming in fear"},
				{"\xF0\x9F\x98\xA8", "fearful face"},
				{"\xF0\x9F\x98\xB0", "anxious face with sweat"},
				{"\xF0\x9F\x98\xA5", "sad but relieved face"},
				{"\xF0\x9F\x98\x93", "downcast face with sweat"},
				{"\xF0\x9F\x98\xAE", "face with open mouth"},
				{"\xF0\x9F\x98\xAF", "hushed face"},
				{"\xF0\x9F\x98\xB2", "astonished face"},
				{"\xF0\x9F\xA5\xBA", "pleading face"},
				{"\xF0\x9F\xA4\xAB", "shushing face"},
				{"\xF0\x9F\xA4\xA5", "lying face"},
				{"\xF0\x9F\xA7\x90", "face with monocle"},
				{"\xF0\x9F\x98\x9B", "face with tongue"},
				{"\xF0\x9F\x98\x9C", "winking face with tongue"},
				{"\xF0\x9F\x98\x9D", "squinting face with tongue"},
			},
			8
		},
		{
			"\xD0\x96\xD0\xB5\xD1\x81\xD1\x82\xD1\x8B",
			{
				{"\xF0\x9F\x91\x8B", "waving hand"},
				{"\xE2\x9C\x8B", "raised hand"},
				{"\xF0\x9F\x96\x90\xEF\xB8\x8F", "hand with fingers splayed"},
				{"\xF0\x9F\xA4\x9A", "raised back of hand"},
				{"\xF0\x9F\x96\x96", "vulcan salute"},
				{"\xF0\x9F\x91\x8C", "OK hand"},
				{"\xE2\x9C\x8C\xEF\xB8\x8F", "victory hand"},
				{"\xF0\x9F\xA4\x9E", "crossed fingers"},
				{"\xF0\x9F\xA4\x98", "sign of the horns"},
				{"\xF0\x9F\xA4\x99", "call me hand"},
				{"\xF0\x9F\x91\x88", "backhand index pointing left"},
				{"\xF0\x9F\x91\x89", "backhand index pointing right"},
				{"\xF0\x9F\x91\x86", "backhand index pointing up"},
				{"\xF0\x9F\x91\x87", "backhand index pointing down"},
				{"\xE2\x98\x9D\xEF\xB8\x8F", "index pointing up"},
				{"\xF0\x9F\x91\x8D", "thumbs up"},
				{"\xF0\x9F\x91\x8E", "thumbs down"},
				{"\xE2\x9C\x8A", "raised fist"},
				{"\xF0\x9F\x91\x8A", "oncoming fist"},
				{"\xF0\x9F\xA4\x9B", "left-facing fist"},
				{"\xF0\x9F\xA4\x9C", "right-facing fist"},
				{"\xF0\x9F\x91\x8F", "clapping hands"},
				{"\xF0\x9F\x99\x8C", "raising hands"},
				{"\xF0\x9F\x91\x90", "open hands"},
				{"\xF0\x9F\x99\x8F", "folded hands"},
				{"\xE2\x9C\x8D\xEF\xB8\x8F", "writing hand"},
				{"\xF0\x9F\x92\xAA", "flexed biceps"},
				{"\xF0\x9F\x99\x8B", "person raising hand"},
				{"\xF0\x9F\x92\x81", "person tipping hand"},
				{"\xF0\x9F\x99\x85", "person gesturing NO"},
				{"\xF0\x9F\x99\x86", "person gesturing OK"},
				{"\xF0\x9F\x99\x87", "person bowing"},
				{"\xF0\x9F\x99\x8D", "person frowning"},
				{"\xF0\x9F\x99\x8E", "person pouting"},
				{"\xF0\x9F\xA4\xA6", "person facepalming"},
				{"\xF0\x9F\xA4\xB7", "person shrugging"},
				{"\xF0\x9F\x9A\xB6", "person walking"},
				{"\xF0\x9F\x8F\x83", "person running"},
				{"\xF0\x9F\x92\x83", "woman dancing"},
				{"\xF0\x9F\x95\xBA", "man dancing"},
				{"\xF0\x9F\x9B\x80", "person taking bath"},
				{"\xF0\x9F\x9B\x8C", "person in bed"},
				{"\xF0\x9F\x91\xA3", "footprints"},
			},
			6
		},
		{
			"\xD0\xAD\xD1\x84\xD1\x84\xD0\xB5\xD0\xBA\xD1\x82\xD1\x8B",
			{
				{"\xF0\x9F\x8E\x89", "party popper"},
				{"\xF0\x9F\x8E\x8A", "confetti ball"},
				{"\xF0\x9F\x8E\x88", "balloon"},
				{"\xF0\x9F\x8E\x81", "wrapped gift"},
				{"\xF0\x9F\x8E\x80", "ribbon"},
				{"\xF0\x9F\x8E\x86", "fireworks"},
				{"\xF0\x9F\x8E\x87", "sparkler"},
				{"\xE2\x9C\xA8", "sparkles"},
				{"\xE2\xAD\x90", "star"},
				{"\xF0\x9F\x8C\x9F", "glowing star"},
				{"\xF0\x9F\x92\xAB", "dizzy"},
				{"\xE2\x9A\xA1", "high voltage"},
				{"\xF0\x9F\x94\xA5", "fire"},
				{"\xF0\x9F\x92\xA5", "collision"},
				{"\xF0\x9F\x92\xA6", "sweat droplets"},
				{"\xF0\x9F\x92\xA8", "dashing away"},
				{"\xF0\x9F\x8E\xB5", "musical note"},
				{"\xF0\x9F\x8E\xB6", "musical notes"},
				{"\xF0\x9F\x8E\xBC", "musical score"},
				{"\xF0\x9F\x8E\xA4", "microphone"},
				{"\xF0\x9F\x9A\x80", "rocket"},
				{"\xF0\x9F\x92\xA1", "light bulb"},
				{"\xF0\x9F\x94\x94", "bell"},
				{"\xF0\x9F\x93\xA3", "cheering megaphone"},
				{"\xF0\x9F\x93\xA2", "loudspeaker"},
				{"\xF0\x9F\x8E\xAF", "bullseye"},
				{"\xF0\x9F\x8E\xB2", "game die"},
				{"\xF0\x9F\x8F\x86", "trophy"},
				{"\xF0\x9F\x91\x91", "crown"},
				{"\xF0\x9F\x92\x8E", "gem stone"},
				{"\xF0\x9F\x8C\x88", "rainbow"},
				{"\xE2\x98\x80\xEF\xB8\x8F", "sun"},
				{"\xF0\x9F\x8C\x99", "crescent moon"},
				{"\xE2\x98\x81\xEF\xB8\x8F", "cloud"},
				{"\xE2\x9D\x84\xEF\xB8\x8F", "snowflake"},
				{"\xE2\x98\x94", "umbrella with rain drops"},
				{"\xF0\x9F\x92\xAF", "hundred points"},
			},
			6
		},
		{
			"\xD0\x96\xD0\xB8\xD0\xB2\xD0\xBE\xD1\x82\xD0\xBD\xD1\x8B\xD0\xB5",
			{
				{"\xF0\x9F\x90\xB5", "monkey face"},
				{"\xF0\x9F\x90\xB6", "dog face"},
				{"\xF0\x9F\x90\xB1", "cat face"},
				{"\xF0\x9F\x90\xAD", "mouse face"},
				{"\xF0\x9F\x90\xB9", "hamster face"},
				{"\xF0\x9F\x90\xB0", "rabbit face"},
				{"\xF0\x9F\x90\xBB", "bear face"},
				{"\xF0\x9F\x90\xBC", "panda face"},
				{"\xF0\x9F\x90\xA8", "koala"},
				{"\xF0\x9F\x90\xAF", "tiger face"},
				{"\xF0\x9F\x90\xAE", "cow face"},
				{"\xF0\x9F\x90\xB7", "pig face"},
				{"\xF0\x9F\x90\xB8", "frog face"},
				{"\xF0\x9F\x90\x92", "monkey"},
				{"\xF0\x9F\x90\x95", "dog"},
				{"\xF0\x9F\x90\x88", "cat"},
				{"\xF0\x9F\x90\xBA", "wolf face"},
				{"\xF0\x9F\x90\xB4", "horse face"},
				{"\xF0\x9F\x90\x8E", "horse"},
				{"\xF0\x9F\x90\x84", "cow"},
				{"\xF0\x9F\x90\x96", "pig"},
				{"\xF0\x9F\x90\x97", "boar"},
				{"\xF0\x9F\x90\x8F", "ram"},
				{"\xF0\x9F\x90\x90", "goat"},
				{"\xF0\x9F\x90\x94", "chicken"},
				{"\xF0\x9F\x90\xA7", "penguin"},
				{"\xF0\x9F\x90\xA6", "bird"},
				{"\xF0\x9F\x90\xA4", "baby chick"},
				{"\xF0\x9F\x90\xA5", "front-facing baby chick"},
				{"\xF0\x9F\x90\x9F", "fish"},
				{"\xF0\x9F\x90\xAC", "dolphin"},
				{"\xF0\x9F\x90\xB3", "spouting whale"},
				{"\xF0\x9F\x90\xA2", "turtle"},
				{"\xF0\x9F\x90\x8D", "snake"},
				{"\xF0\x9F\x90\x99", "octopus"},
				{"\xF0\x9F\x90\x9B", "bug"},
				{"\xF0\x9F\x90\x9D", "honeybee"},
				{"\xF0\x9F\x90\x9E", "lady beetle"},
				{"\xF0\x9F\xA6\x8B", "butterfly"},
				{"\xF0\x9F\x90\x8C", "snail"},
				{"\xF0\x9F\x90\x98", "elephant"},
				{"\xF0\x9F\x90\xBE", "paw prints"},
			},
			6
		},
		{
			"\xD0\xA1\xD0\xB5\xD1\x80\xD0\xB4\xD1\x86\xD0\xB0",
			{
				{"\xF0\x9F\x98\x8D", "smiling face with heart-eyes"},
				{"\xF0\x9F\x98\x98", "face blowing a kiss"},
				{"\xF0\x9F\x98\xBB", "smiling cat face with heart-eyes"},
				{"\xF0\x9F\x92\x8B", "kiss mark"},
				{"\xF0\x9F\x92\x8C", "love letter"},
				{"\xF0\x9F\x92\x98", "heart with arrow"},
				{"\xE2\x9D\xA4\xEF\xB8\x8F", "red heart"},
				{"\xF0\x9F\x92\x93", "beating heart"},
				{"\xF0\x9F\x92\x94", "broken heart"},
				{"\xF0\x9F\x92\x95", "two hearts"},
				{"\xF0\x9F\x92\x96", "sparkling heart"},
				{"\xF0\x9F\x92\x97", "growing heart"},
				{"\xF0\x9F\x92\x99", "blue heart"},
				{"\xF0\x9F\x92\x9A", "green heart"},
				{"\xF0\x9F\x92\x9B", "yellow heart"},
				{"\xF0\x9F\xA7\xA1", "orange heart"},
				{"\xF0\x9F\x92\x9C", "purple heart"},
				{"\xF0\x9F\x96\xA4", "black heart"},
				{"\xF0\x9F\x92\x9D", "heart with ribbon"},
				{"\xF0\x9F\x92\x9E", "revolving hearts"},
				{"\xF0\x9F\x92\x9F", "heart decoration"},
				{"\xE2\x9D\xA3\xEF\xB8\x8F", "heavy heart exclamation"},
				{"\xE2\x99\xA5\xEF\xB8\x8F", "heart suit"},
				{"\xF0\x9F\x92\x91", "couple with heart"},
				{"\xF0\x9F\x92\x8F", "kiss"},
			},
			6
		}
	};
	return categories;
}


inline const std::vector<std::string_view>& supportedEmoji()
{
	static const std::vector<std::string_view> emojis = []() {
		std::vector<std::string_view> out;
		const auto& categories = emojiCategories();
		for(size_t i=0; i<categories.size(); ++i)
			for(size_t z=0; z<categories[i].emojis.size(); ++z)
				out.push_back(categories[i].emojis[z].emoji);
		return out;
	}();
	return emojis;
}


inline std::string_view emojiDisplayName(std::string_view text)
{
	const auto& categories = emojiCategories();
	for(size_t i=0; i<categories.size(); ++i)
	{
		for(size_t z=0; z<categories[i].emojis.size(); ++z)
		{
			if(categories[i].emojis[z].emoji == text)
				return categories[i].emojis[z].name;
		}
	}

	return "emoji";
}


inline std::vector<EmojiPickerCategory> buildPickerCategories(const std::vector<std::string>& recent_emojis)
{
	std::vector<EmojiPickerCategory> out;
	out.reserve(1 + emojiCategories().size());

	EmojiPickerCategory recent_category;
	recent_category.title = std::string(recentCategoryTitle());
	recent_category.num_columns = recentEmojiColumns();
	for(size_t i=0; i<recent_emojis.size(); ++i)
	{
		if(!isSupportedEmoji(recent_emojis[i]))
			continue;

		bool duplicate = false;
		for(size_t z=0; z<recent_category.emojis.size(); ++z)
		{
			if(recent_category.emojis[z] == recent_emojis[i])
			{
				duplicate = true;
				break;
			}
		}

		if(!duplicate)
			recent_category.emojis.push_back(recent_emojis[i]);

		if(recent_category.emojis.size() >= maxRecentEmojiCount())
			break;
	}
	out.push_back(recent_category);

	const auto& categories = emojiCategories();
	for(size_t i=0; i<categories.size(); ++i)
	{
		EmojiPickerCategory picker_category;
		picker_category.title = std::string(categories[i].title);
		picker_category.num_columns = categories[i].num_columns;
		picker_category.emojis.reserve(categories[i].emojis.size());
		for(size_t z=0; z<categories[i].emojis.size(); ++z)
			picker_category.emojis.push_back(std::string(categories[i].emojis[z].emoji));
		out.push_back(picker_category);
	}

	return out;
}


inline std::string pickerButtonLabel()
{
	return std::string("\xF0\x9F\x98\x80");
}


inline bool isSupportedEmoji(std::string_view text)
{
	const auto& emojis = supportedEmoji();
	for(size_t i=0; i<emojis.size(); ++i)
	{
		if(emojis[i] == text)
			return true;
	}
	return false;
}


inline void test()
{
	assert(emojiCategories().size() == 6);
	assert(supportedEmoji().size() >= 200);
	assert(isSupportedEmoji(supportedEmoji()[0]));
	assert(isSupportedEmoji(supportedEmoji()[supportedEmoji().size() - 1]));
	assert(emojiDisplayName(supportedEmoji()[0]) != "emoji");
	assert(!isSupportedEmoji(""));
	assert(!isSupportedEmoji("hello"));
	assert(!isSupportedEmoji(std::string(supportedEmoji()[0]) + "!"));

	for(size_t i=0; i<supportedEmoji().size(); ++i)
		assert(supportedEmoji()[i].find("\xE2\x80\x8D") == std::string_view::npos);
}

}
