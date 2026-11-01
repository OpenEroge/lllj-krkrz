// toml.dll replacement: Scripts.tomlDecodeFromStorage(storage) -> Dictionary
#include "tjsCommHead.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "TextStream.h"
#include "MsgIntf.h"
#include "lllj_compat.h"

#define TOML_EXCEPTIONS 1
#include "toml.hpp"

#include <memory>
#include <sstream>
#include <string>

static ttstr Utf8ToTTStr(const std::string &s)
{
	std::u16string out;
	out.reserve(s.size());
	size_t i = 0;
	while (i < s.size()) {
		unsigned char c = (unsigned char)s[i];
		char32_t cp;
		int n;
		if (c < 0x80) { cp = c; n = 1; }
		else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
		else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; }
		else { cp = c & 0x07; n = 4; }
		for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3f);
		i += n;
		if (cp >= 0x10000) {
			cp -= 0x10000;
			out.push_back((char16_t)(0xd800 + (cp >> 10)));
			out.push_back((char16_t)(0xdc00 + (cp & 0x3ff)));
		} else {
			out.push_back((char16_t)cp);
		}
	}
	return ttstr((const tjs_char *)out.c_str());
}

static std::string TTStrToUtf8(const ttstr &s)
{
	std::string out;
	const tjs_char *p = s.c_str();
	while (*p) {
		char32_t cp = (char32_t)*p++;
		if (cp >= 0xd800 && cp < 0xdc00 && *p >= 0xdc00 && *p < 0xe000) {
			cp = 0x10000 + ((cp - 0xd800) << 10) + ((char32_t)*p++ - 0xdc00);
		}
		if (cp < 0x80) out += (char)cp;
		else if (cp < 0x800) { out += (char)(0xc0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3f)); }
		else if (cp < 0x10000) { out += (char)(0xe0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3f)); out += (char)(0x80 | (cp & 0x3f)); }
		else { out += (char)(0xf0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3f)); out += (char)(0x80 | ((cp >> 6) & 0x3f)); out += (char)(0x80 | (cp & 0x3f)); }
	}
	return out;
}

static tTJSVariant ToVariant(const toml::node &node);

static tTJSVariant TableToVariant(const toml::table &tbl)
{
	iTJSDispatch2 *dict = TJSCreateDictionaryObject();
	for (auto &&[k, v] : tbl) {
		tTJSVariant val = ToVariant(v);
		ttstr key = Utf8ToTTStr(std::string(k.str()));
		dict->PropSet(TJS_MEMBERENSURE, key.c_str(), nullptr, &val, dict);
	}
	tTJSVariant r(dict, dict);
	dict->Release();
	return r;
}

static tTJSVariant ArrayToVariant(const toml::array &arr)
{
	iTJSDispatch2 *array = TJSCreateArrayObject();
	tjs_int i = 0;
	for (auto &&v : arr) {
		tTJSVariant val = ToVariant(v);
		array->PropSetByNum(TJS_MEMBERENSURE, i++, &val, array);
	}
	tTJSVariant r(array, array);
	array->Release();
	return r;
}

static tTJSVariant ToVariant(const toml::node &node)
{
	if (auto t = node.as_table()) return TableToVariant(*t);
	if (auto a = node.as_array()) return ArrayToVariant(*a);
	if (auto s = node.as_string()) return tTJSVariant(Utf8ToTTStr(s->get()));
	if (auto i = node.as_integer()) return tTJSVariant((tjs_int64)i->get());
	if (auto f = node.as_floating_point()) return tTJSVariant((tjs_real)f->get());
	if (auto b = node.as_boolean()) return tTJSVariant((tjs_int)(b->get() ? 1 : 0));
	std::ostringstream ss;
	if (auto d = node.as_date()) ss << d->get();
	else if (auto t = node.as_time()) ss << t->get();
	else if (auto dt = node.as_date_time()) ss << dt->get();
	return tTJSVariant(Utf8ToTTStr(ss.str()));
}

static ttstr ReadAllText(const ttstr &storage)
{
	std::unique_ptr<iTJSTextReadStream, void (*)(iTJSTextReadStream *)> stream(
		TVPCreateTextStreamForRead(storage, TJS_W("")),
		[](iTJSTextReadStream *s) { if (s) s->Destruct(); });
	ttstr text, buf;
	while (stream->Read(buf, 0x10000) > 0) text += buf;
	return text;
}

LLLJ_NATIVE(tomlDecodeFromStorage)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	ttstr storage = *param[0];
	std::string src = TTStrToUtf8(ReadAllText(storage));
	try {
		toml::table tbl = toml::parse(src, std::string_view(TTStrToUtf8(storage)));
		if (result) *result = TableToVariant(tbl);
	} catch (const toml::parse_error &e) {
		std::ostringstream ss;
		ss << "toml parse error: " << e.description() << " (" << e.source().begin << ")";
		TVPThrowExceptionMessage(Utf8ToTTStr(ss.str()).c_str());
	}
	return TJS_S_OK;
}
