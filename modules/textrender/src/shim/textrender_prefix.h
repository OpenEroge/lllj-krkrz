#pragma once
// PORT: force-included into textrender.cpp (see CMakeLists.txt). Names the
// module for ncbind (TVP_PLUGIN_NAME) and bridges krkrsdl3's string model.
//
// krkrsdl3's tjs_char is char and ttstr holds UTF-8, so its textrender works on
// std::string and builds ttstr from narrow strings. In krkrz tjs_char is
// char16_t and ttstr(const char *) decodes Shift_JIS, so every such crossing in
// textrender.cpp goes through the helpers below instead (marked "PORT").
#define TVP_PLUGIN_NAME compat_textrender
#include "compat/ncbind.h"
#include "CharacterSet.h"
#include "FontRasterizer.h"
#include <string>

typedef tjs_char tjs_wchar; // krkrsdl3: the UTF-16 code unit type
#undef TJS_N // krkrz: narrow (Shift_JIS when converted to ttstr)
#define TJS_N(x) TJS_W(x)

namespace compat_textrender {

inline ttstr U16(const std::string &utf8)
{
	tjs_string wide;
	TVPUtf8ToUtf16(wide, utf8);
	return ttstr(wide.c_str());
}

inline ttstr U16(const char *utf8) { return U16(std::string(utf8 ? utf8 : "")); }

inline std::string U8(const ttstr &text)
{
	std::string out;
	if (!text.IsEmpty()) TVPUtf16ToUtf8(out, tjs_string(text.c_str()));
	return out;
}

// krkrsdl3's TVP_utf8_to_utf16: one UTF-8 character to one UTF-16 unit.
inline bool Utf8CharToUnit(const char *in, tjs_char *out)
{
	tjs_string wide;
	if (!TVPUtf8ToUtf16(wide, std::string(in)) || wide.empty()) return false;
	*out = wide[0];
	return true;
}

} // namespace compat_textrender
