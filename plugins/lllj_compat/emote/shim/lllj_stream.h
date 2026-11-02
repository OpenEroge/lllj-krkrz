#pragma once
// LLLJ: krkrsdl2's tTJSBinaryStream had ReadBuffer/SetPosition/ReadI32LE/...
// members; in krkrz it is the bare iTJSBinaryStream interface and the helpers
// are free functions (tjs.h). Call sites were rewritten to those; only the
// 8-bit reader has no krkrz counterpart.
#include "tjs.h"

namespace TJS {
inline tjs_uint8 TVPReadI8LE(iTJSBinaryStream *stream)
{
	tjs_uint8 v;
	TVPReadBuffer(stream, &v, 1);
	return v;
}
} // namespace TJS
