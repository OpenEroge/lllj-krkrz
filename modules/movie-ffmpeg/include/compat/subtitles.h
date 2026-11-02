// movie-ffmpeg: subtitle sources. The module renders ASS subtitles (libass)
// onto decoded movie frames; other modules or game profiles supply them.
#pragma once
#include <string>
#include <vector>

namespace compat {

struct SubtitleScript {
	std::string name;                 // for logs and CompatNative.movieStats()
	std::vector<unsigned char> data;  // ASS script, UTF-8
};
struct SubtitleFont {
	std::string family;
	std::vector<unsigned char> data;  // TTF/OTF
};

// Main thread, whenever a movie is opened (so the answer may follow the
// current language). movie is the opened path in UTF-8. Return true and fill
// script to draw subtitles; the first provider that returns true wins.
typedef bool (*SubtitleProvider)(const std::string &movie, SubtitleScript &script);
// Called once, before the first script is loaded: fonts registered with libass
// only (fontconfig remains the fallback).
typedef void (*SubtitleFontProvider)(std::vector<SubtitleFont> &fonts);

void AddSubtitleProvider(SubtitleProvider provider);
void AddSubtitleFontProvider(SubtitleFontProvider provider);

} // namespace compat
