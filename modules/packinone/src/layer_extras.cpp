// PackinOne natives that games call without guards (prelude/packinone.tjs
// installs them):
//   Layer.fetchImageSize       PackinOne
//   Storages.saveOctet         PackinOne
//   System.getKnownFolderPath  PackinOne (snapcapture's "Captures" folder)
#include "tjsCommHead.h"
#include "tjsNative.h"
#include "tjsArray.h"
#include "CharacterSet.h"
#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "MsgIntf.h"
#include "StorageIntf.h"
#include "compat/compat.h"
#include "compat/layer.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace {

// XDG pictures directory (user-dirs.dirs, default ~/Pictures) if it exists, else $HOME.
std::string PicturesDir()
{
	const char *home = std::getenv("HOME");
	if (!home || !*home) return std::string();
	const std::string h = home;
	std::string dir = h + "/Pictures";
	const char *config = std::getenv("XDG_CONFIG_HOME");
	std::ifstream in((config && *config ? std::string(config) : h + "/.config") + "/user-dirs.dirs");
	const std::string key = "XDG_PICTURES_DIR=\"";
	for (std::string line; std::getline(in, line);) {
		if (line.size() <= key.size() || line.compare(0, key.size(), key) != 0 || line.back() != '"')
			continue;
		std::string value = line.substr(key.size(), line.size() - key.size() - 1);
		if (value == "$HOME") value = h;
		else if (value.compare(0, 6, "$HOME/") == 0) value = h + value.substr(5);
		dir = value;
		break;
	}
	struct stat st;
	return (stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ? dir : h;
}

} // namespace

// Layer.fetchImageSize(storage) -> [width, height]. Void when the image cannot be
// loaded, which is what the callers test for. The image is decoded by
// TVPLoadGraphic, the path Layer.loadImages uses, so names resolve identically
// (auto path, missing extension).
COMPAT_NATIVE(fetchImageSize)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	int width, height;
	if (!Layer_FetchImageSize(ttstr(*param[0]), width, height)) {
		if (result) result->Clear(); // void, not the VM's default integer
		return TJS_S_OK;
	}
	iTJSDispatch2 *array = TJSCreateArrayObject();
	tTJSVariant r(array, array);
	array->Release();
	tTJSVariant w((tjs_int)width), h((tjs_int)height);
	array->PropSetByNum(TJS_MEMBERENSURE, 0, &w, array);
	array->PropSetByNum(TJS_MEMBERENSURE, 1, &h, array);
	if (result) *result = r;
	return TJS_S_OK;
}

// Storages.saveOctet(storage, octet): replaces the storage with the octet's bytes.
COMPAT_NATIVE(saveOctet)
{
	if (numparams < 2) return TJS_E_BADPARAMCOUNT;
	if (param[1]->Type() != tvtOctet) TVPThrowExceptionMessage(TJS_W("saveOctet: octet expected"));
	tTJSVariantOctet *octet = param[1]->AsOctetNoAddRef();
	std::unique_ptr<tTJSBinaryStream> s(TVPCreateStream(ttstr(*param[0]), TJS_BS_WRITE));
	if (octet->GetLength())
		TJS::TVPWriteBuffer(s.get(), octet->GetData(), (tjs_uint)octet->GetLength());
	return TJS_S_OK;
}

// System.getKnownFolderPath(name, flags) -> folder path, or "" when unknown (the
// game's failure value). Flags are ignored; only "Captures" is mapped.
COMPAT_NATIVE(getKnownFolderPath)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	std::string dir;
	if (ttstr(*param[0]) == TJS_W("Captures")) dir = PicturesDir();
	tjs_string wide;
	TVPUtf8ToUtf16(wide, dir);
	if (result) *result = ttstr(wide.c_str());
	return TJS_S_OK;
}
