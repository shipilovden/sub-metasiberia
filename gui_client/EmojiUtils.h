#pragma once


#include <array>
#include <cassert>
#include <string>
#include <string_view>
#include <vector>


namespace EmojiUtils
{

struct EmojiCategory
{
	std::string_view title;
	const std::string_view* emojis;
	size_t num_emojis;
	int num_columns;
};


inline const std::array<std::string_view, 24>& faceEmoji()
{
	static const std::array<std::string_view, 24> emojis = {
		"\xF0\x9F\x98\x80", // grinning face
		"\xF0\x9F\x98\x81", // beaming face
		"\xF0\x9F\x98\x82", // tears of joy
		"\xF0\x9F\x98\x83", // smiling face
		"\xF0\x9F\x98\x84", // smiling face with open mouth
		"\xF0\x9F\x98\x85", // smiling face with sweat
		"\xF0\x9F\x98\x86", // grinning squinting face
		"\xF0\x9F\x98\x89", // winking face
		"\xF0\x9F\x98\x8A", // smiling face with smiling eyes
		"\xF0\x9F\x98\x87", // smiling face with halo
		"\xF0\x9F\xA5\xB0", // smiling face with hearts
		"\xF0\x9F\x98\x8B", // face savoring food
		"\xF0\x9F\x98\x8E", // smiling face with sunglasses
		"\xF0\x9F\x98\x8D", // heart eyes
		"\xF0\x9F\x98\x98", // face blowing a kiss
		"\xF0\x9F\x98\x97", // kissing face
		"\xF0\x9F\x98\x99", // kissing face with smiling eyes
		"\xF0\x9F\x98\x9A", // kissing face with closed eyes
		"\xF0\x9F\x99\x82", // slightly smiling face
		"\xF0\x9F\xA4\x97", // hugging face
		"\xF0\x9F\xA4\xA9", // star-struck
		"\xF0\x9F\xA5\xB3", // partying face
		"\xF0\x9F\xA4\xA0", // cowboy hat face
		"\xF0\x9F\x98\x8F"  // smirking face
	};
	return emojis;
}


inline const std::array<std::string_view, 48>& moodEmoji()
{
	static const std::array<std::string_view, 48> emojis = {
		"\xF0\x9F\xA4\x94", // thinking face
		"\xF0\x9F\xA4\xA8", // face with raised eyebrow
		"\xF0\x9F\x98\x90", // neutral face
		"\xF0\x9F\x98\x91", // expressionless face
		"\xF0\x9F\x98\xB6", // face without mouth
		"\xF0\x9F\x99\x84", // face with rolling eyes
		"\xF0\x9F\x98\xAC", // grimacing face
		"\xF0\x9F\x98\x8C", // relieved face
		"\xF0\x9F\x98\x94", // pensive face
		"\xF0\x9F\x98\xAA", // sleepy face
		"\xF0\x9F\x98\xB4", // sleeping face
		"\xF0\x9F\x98\xB7", // face with medical mask
		"\xF0\x9F\xA4\x92", // face with thermometer
		"\xF0\x9F\xA4\x95", // face with head bandage
		"\xF0\x9F\xA4\xA2", // nauseated face
		"\xF0\x9F\xA4\xAE", // face vomiting
		"\xF0\x9F\xA4\xA7", // sneezing face
		"\xF0\x9F\x98\xB5", // dizzy face
		"\xF0\x9F\xA4\xAF", // exploding head
		"\xF0\x9F\x98\x95", // confused face
		"\xF0\x9F\x99\x81", // slightly frowning face
		"\xE2\x98\xB9\xEF\xB8\x8F", // frowning face
		"\xF0\x9F\x98\xA3", // persevering face
		"\xF0\x9F\x98\x96", // confounded face
		"\xF0\x9F\x98\xAB", // tired face
		"\xF0\x9F\x98\xA9", // weary face
		"\xF0\x9F\x98\xA2", // crying face
		"\xF0\x9F\x98\xAD", // loudly crying face
		"\xF0\x9F\x98\xA4", // face with steam from nose
		"\xF0\x9F\x98\xA0", // angry face
		"\xF0\x9F\x98\xA1", // pouting face
		"\xF0\x9F\xA4\xAC", // face with symbols on mouth
		"\xF0\x9F\x98\xB3", // flushed face
		"\xF0\x9F\x98\xB1", // face screaming in fear
		"\xF0\x9F\x98\xA8", // fearful face
		"\xF0\x9F\x98\xB0", // anxious face with sweat
		"\xF0\x9F\x98\xA5", // sad but relieved face
		"\xF0\x9F\x98\x93", // downcast face with sweat
		"\xF0\x9F\x98\xAE", // face with open mouth
		"\xF0\x9F\x98\xAF", // hushed face
		"\xF0\x9F\x98\xB2", // astonished face
		"\xF0\x9F\xA5\xBA", // pleading face
		"\xF0\x9F\xA4\xAB", // shushing face
		"\xF0\x9F\xA4\xA5", // lying face
		"\xF0\x9F\xA7\x90", // face with monocle
		"\xF0\x9F\x98\x9B", // face with tongue
		"\xF0\x9F\x98\x9C", // winking face with tongue
		"\xF0\x9F\x98\x9D"  // squinting face with tongue
	};
	return emojis;
}


inline const std::array<std::string_view, 12>& handEmoji()
{
	static const std::array<std::string_view, 12> emojis = {
		"\xF0\x9F\x91\x8D",             // thumbs up
		"\xF0\x9F\x91\x8E",             // thumbs down
		"\xF0\x9F\x91\x8B",             // waving hand
		"\xF0\x9F\x99\x8C",             // raising hands
		"\xF0\x9F\x91\x8F",             // clapping hands
		"\xF0\x9F\x99\x8F",             // folded hands
		"\xF0\x9F\x91\x8C",             // OK hand
		"\xF0\x9F\x91\x8A",             // oncoming fist
		"\xF0\x9F\xA4\x9D",             // handshake
		"\xE2\x9C\x8C\xEF\xB8\x8F",     // victory hand
		"\xF0\x9F\xA4\x9E",             // crossed fingers
		"\xF0\x9F\xA4\x98"              // sign of the horns
	};
	return emojis;
}


inline const std::array<std::string_view, 20>& effectEmoji()
{
	static const std::array<std::string_view, 20> emojis = {
		"\xF0\x9F\x8E\x89", // party popper
		"\xF0\x9F\x94\xA5", // fire
		"\xF0\x9F\x92\xAF", // hundred points
		"\xE2\xAD\x90",     // star
		"\xF0\x9F\x9A\x80", // rocket
		"\xF0\x9F\x8E\xB5", // musical note
		"\xF0\x9F\x92\xA1", // light bulb
		"\xF0\x9F\x8C\x9E", // sun with face
		"\xF0\x9F\x8C\x9D", // full moon with face
		"\xF0\x9F\x8C\x9A", // new moon with face
		"\xF0\x9F\x8E\x83", // jack-o-lantern
		"\xF0\x9F\x91\xB6", // baby
		"\xF0\x9F\x91\xB9", // ogre
		"\xF0\x9F\x92\x80", // skull
		"\xF0\x9F\x91\xBD", // alien
		"\xF0\x9F\xA4\x96", // robot
		"\xF0\x9F\x92\xA9", // pile of poo
		"\xE2\x9A\xA1",     // high voltage
		"\xE2\x9C\xA8",     // sparkles
		"\xF0\x9F\x8E\x88"  // balloon
	};
	return emojis;
}


inline const std::array<std::string_view, 12>& animalEmoji()
{
	static const std::array<std::string_view, 12> emojis = {
		"\xF0\x9F\x98\xBA", // grinning cat
		"\xF0\x9F\x98\xB8", // grinning cat with smiling eyes
		"\xF0\x9F\x98\xB9", // cat with tears of joy
		"\xF0\x9F\x98\xBB", // smiling cat with heart-eyes
		"\xF0\x9F\x98\xBC", // cat with wry smile
		"\xF0\x9F\x98\xBD", // kissing cat
		"\xF0\x9F\x99\x80", // weary cat
		"\xF0\x9F\x98\xBF", // crying cat
		"\xF0\x9F\x98\xBE", // pouting cat
		"\xF0\x9F\x90\xB6", // dog face
		"\xF0\x9F\x90\xB1", // cat face
		"\xF0\x9F\x90\xBC"  // panda
	};
	return emojis;
}


inline const std::array<std::string_view, 14>& heartEmoji()
{
	static const std::array<std::string_view, 14> emojis = {
		"\xE2\x9D\xA4\xEF\xB8\x8F", // red heart
		"\xF0\x9F\xA7\xA1",         // orange heart
		"\xF0\x9F\x92\x9B",         // yellow heart
		"\xF0\x9F\x92\x9A",         // green heart
		"\xF0\x9F\x92\x99",         // blue heart
		"\xF0\x9F\x92\x9C",         // purple heart
		"\xF0\x9F\x96\xA4",         // black heart
		"\xF0\x9F\xA4\x8D",         // white heart
		"\xF0\x9F\xA4\x8E",         // brown heart
		"\xF0\x9F\x92\x94",         // broken heart
		"\xF0\x9F\x92\x96",         // sparkling heart
		"\xF0\x9F\x92\x98",         // heart with arrow
		"\xF0\x9F\x92\x9D",         // heart with ribbon
		"\xF0\x9F\x92\x9E"          // revolving hearts
	};
	return emojis;
}


inline const std::array<EmojiCategory, 6>& emojiCategories()
{
	static const std::array<EmojiCategory, 6> categories = {{
		{ "\xD0\x9B\xD0\xB8\xD1\x86\xD0\xB0", faceEmoji().data(),   faceEmoji().size(),   7 },
		{ "\xD0\xAD\xD0\xBC\xD0\xBE\xD1\x86\xD0\xB8\xD0\xB8", moodEmoji().data(),   moodEmoji().size(),   7 },
		{ "\xD0\x96\xD0\xB5\xD1\x81\xD1\x82\xD1\x8B", handEmoji().data(),   handEmoji().size(),   4 },
		{ "\xD0\xAD\xD1\x84\xD1\x84\xD0\xB5\xD0\xBA\xD1\x82\xD1\x8B", effectEmoji().data(), effectEmoji().size(), 5 },
		{ "\xD0\x96\xD0\xB8\xD0\xB2\xD0\xBE\xD1\x82\xD0\xBD\xD1\x8B\xD0\xB5", animalEmoji().data(), animalEmoji().size(), 4 },
		{ "\xD0\xA1\xD0\xB5\xD1\x80\xD0\xB4\xD1\x86\xD0\xB0", heartEmoji().data(),  heartEmoji().size(),  4 }
	}};
	return categories;
}


inline const std::vector<std::string_view>& supportedEmoji()
{
	static const std::vector<std::string_view> emojis = []() {
		std::vector<std::string_view> out;
		const auto& categories = emojiCategories();
		for(size_t i=0; i<categories.size(); ++i)
			for(size_t z=0; z<categories[i].num_emojis; ++z)
				out.push_back(categories[i].emojis[z]);
		return out;
	}();
	return emojis;
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
	assert(supportedEmoji().size() == 130);
	assert(isSupportedEmoji(supportedEmoji()[0]));
	assert(isSupportedEmoji(supportedEmoji()[supportedEmoji().size() - 1]));
	assert(!isSupportedEmoji(""));
	assert(!isSupportedEmoji("hello"));
	assert(!isSupportedEmoji(std::string(supportedEmoji()[0]) + "!"));
}

}
