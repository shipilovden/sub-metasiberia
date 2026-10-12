#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H
#include <map>
#include <set>
#include <string>
#include <vector>

// Qt-independent coverage catalogue. Faces are closed after discovery, so
// filtering hundreds of fonts does not retain hundreds of FreeType faces.
class FontCoverage
{
public:
	FontCoverage() : library(NULL) { FT_Init_FreeType(&library); }
	~FontCoverage() { if(library) FT_Done_FreeType(library); }
	FontCoverage(const FontCoverage&) = delete;
	FontCoverage& operator=(const FontCoverage&) = delete;
	struct Entry { bool loaded = false; std::set<unsigned int> characters; };
	bool cached(const std::string& path) const { return entries.find(path) != entries.end(); }
	const Entry& get(const std::string& path)
	{
		auto found = entries.find(path);
		if(found != entries.end()) return found->second;
		Entry& entry = entries[path];
		FT_Face face = NULL;
		if(!library || path.empty() || FT_New_Face(library, path.c_str(), 0, &face)) return entry;
		entry.loaded = FT_Select_Charmap(face, FT_ENCODING_UNICODE) == 0;
		if(entry.loaded)
		{
			FT_UInt glyph = 0;
			for(FT_ULong cp = FT_Get_First_Char(face, &glyph); glyph != 0; cp = FT_Get_Next_Char(face, cp, &glyph))
				entry.characters.insert((unsigned int)cp);
		}
		FT_Done_Face(face);
		return entry;
	}
	static bool layoutControl(unsigned int cp) { return cp == '\n' || cp == '\r'; }
	template<class Codepoints> bool supports(const std::string& path, const Codepoints& text)
	{
		const Entry& entry = get(path);
		if(!entry.loaded) return false;
		for(const auto cp : text)
			if(!layoutControl(cp) && entry.characters.count(cp) == 0) return false;
		return true;
	}
private:
	FT_Library library;
	std::map<std::string, Entry> entries;
};
