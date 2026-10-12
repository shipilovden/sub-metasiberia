// Standalone audit against the same FreeType objects used by gui_client.
#include <ft2build.h>
#include FT_FREETYPE_H
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
	if(argc != 2) return 2;
	FT_Library library;
	if(FT_Init_FreeType(&library)) return 2;
	int checked = 0, failed = 0, incomplete = 0;
	const std::u32string sample = U"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя";
	for(const auto& entry : std::filesystem::directory_iterator(argv[1]))
	{
		const auto extension = entry.path().extension().string();
		if(extension != ".ttf" && extension != ".otf" && extension != ".woff" && extension != ".fon") continue;
		++checked;
		FT_Face face;
		const FT_Error error = FT_New_Face(library, entry.path().string().c_str(), 0, &face);
		if(error) { ++failed; std::cout << entry.path().filename().string() << ": load error " << error << '\n'; continue; }
		bool bad = FT_Set_Char_Size(face, 0, 20 * 64, 96, 96) != 0;
		int missing = 0;
		for(char32_t codepoint : sample)
		{
			const FT_UInt index = FT_Get_Char_Index(face, codepoint);
			if(!index) { ++missing; continue; }
			if(FT_Load_Glyph(face, index, FT_LOAD_DEFAULT) || FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) bad = true;
		}
		if(bad) { ++failed; std::cout << entry.path().filename().string() << ": render error\n"; }
		if(missing) { ++incomplete; std::cout << entry.path().filename().string() << ": missing " << missing << " sample characters\n"; }
		FT_Done_Face(face);
	}
	FT_Done_FreeType(library);
	std::cout << "Fonts checked=" << checked << " load/render failures=" << failed << " incomplete character sets=" << incomplete << '\n';
	return checked == 0 || failed ? 1 : 0;
}
