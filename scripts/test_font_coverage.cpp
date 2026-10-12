// Offline check: link against the same FreeType objects as the native client.
#include "../gui_client/FontCoverage.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) { if(!value) throw std::runtime_error(message); }

int main(int argc, char** argv)
{
	try
	{
		require(argc == 2, "Pass the bundled font directory.");
		FontCoverage coverage;
		const std::u32string russian = U"Привет Ёжик 0123456789";
		const std::u32string latin = U"Hello 0123456789";
		const auto started = std::chrono::steady_clock::now();
		int loaded = 0, russian_supported = 0, latin_supported = 0, incomplete = 0;
		std::string complete_font;
		for(const auto& file : std::filesystem::directory_iterator(argv[1]))
		{
			const auto ext = file.path().extension().string();
			if(ext != ".ttf" && ext != ".otf" && ext != ".woff" && ext != ".fon") continue;
			const auto path = file.path().string();
			const auto& entry = coverage.get(path);
			require(entry.loaded, "A bundled font failed Unicode coverage loading.");
			require(&entry == &coverage.get(path), "Coverage cache did not preserve its entry.");
			++loaded;
			if(coverage.supports(path, russian)) { ++russian_supported; complete_font = path; }
			else ++incomplete;
			if(coverage.supports(path, latin)) ++latin_supported;
			require(!coverage.supports(path, std::u32string(1, (char32_t)0x10FFFF)), "Unsupported Unicode scalar was accepted.");
		}
		require(loaded > 0 && russian_supported > 0 && incomplete > 0, "Expected both compatible and incompatible fonts.");
		require(coverage.supports(complete_font, std::u32string(U"Привет\r\nЁжик")), "Multiline layout controls were rejected.");
		require(!coverage.supports("does-not-exist.ttf", russian), "Missing font was accepted.");
		require(!coverage.supports("", std::u32string()), "Missing font was accepted for empty content.");
		std::cout << "Coverage PASS: loaded=" << loaded << " Russian=" << russian_supported
			<< " Latin=" << latin_supported << " incompatible=" << incomplete << " elapsed_ms="
			<< std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count() << '\n';
		return 0;
	}
	catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
