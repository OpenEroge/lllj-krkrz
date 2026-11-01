/* SPDX-License-Identifier: MIT */
#ifndef KRKR_SHARED_MEMORY_STREAM_H
#define KRKR_SHARED_MEMORY_STREAM_H

#include "tjs.h"
#include "SharedByteView.h"

class tLLLJSharedMemoryStream final : public TJS::tTJSBinaryStream
{
    KrkrReadOnlyCursor Cursor;
public:
    explicit tLLLJSharedMemoryStream(KrkrSharedBytes bytes) : Cursor(std::move(bytes)) {}
    tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int origin) override
        { return Cursor.Seek(offset, origin); }
    tjs_uint TJS_INTF_METHOD Read(void *buffer, tjs_uint count) override
        { return static_cast<tjs_uint>(Cursor.Read(buffer, count)); }
    tjs_uint TJS_INTF_METHOD Write(const void *, tjs_uint) override { return 0; }
    tjs_uint64 TJS_INTF_METHOD GetSize() override { return Cursor.Size(); }
    void TJS_INTF_METHOD SetEndOfStorage() override {}
};

#endif
