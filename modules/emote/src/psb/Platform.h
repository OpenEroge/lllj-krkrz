#pragma once
// PORT: compatibility bridge for code ported from krkrsdl3 / krkr-ns
#include "DebugIntf.h"
#include <cstdio>
#define TVPConsoleLog(...) do { char _compat_buf[512]; std::snprintf(_compat_buf, sizeof(_compat_buf), __VA_ARGS__); TVPAddLog(ttstr(TJS_W("(compat) ")) + ttstr(_compat_buf)); } while (0)
