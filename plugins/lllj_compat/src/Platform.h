#pragma once
// LLLJ: compatibility bridge for code ported from krkrsdl3 / krkr-ns
#include "DebugIntf.h"
#include <cstdio>
#define TVPConsoleLog(...) do { char _lllj_buf[512]; std::snprintf(_lllj_buf, sizeof(_lllj_buf), __VA_ARGS__); TVPAddLog(ttstr(TJS_W("(lllj) ")) + ttstr(_lllj_buf)); } while (0)
