#pragma once
// PORT: stands in for krkr-ns' TVPSysFont.h, which is the win32 font table.
// The generic build only has the font list function (generic/environ/
// FontSystemBase.cpp); the rest of that header is not used by layerExDraw.
#include <vector>
#include "tjsCommHead.h"

extern void TVPGetAllFontList(std::vector<tjs_string> &list);
