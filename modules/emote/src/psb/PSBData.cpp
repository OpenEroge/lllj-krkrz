#include "tjsCommHead.h"
#include "PSBData.h"
#include "Platform.h"

namespace PSB
{

bool PSBHeader::isEncrypted() const
{
    return offsetEncrypt > MAX_LENGTH + 16 || offsetNames == 0 ||
           (version > 1 && offsetEncrypt != offsetNames && offsetEncrypt != 0);
}

tjs_uint32 PSBHeader::getHeaderLength() const
{
    if (version < 3)
        return 40u;
    if (version > 3)
        return 56u;
    return 44u;
}

bool PSBHeader::parsePSBHeader(TJS::tTJSBinaryStream* stream)
{
    TJS::TVPReadBuffer(stream, signature, 4);
    TJS::TVPReadBuffer(stream, &version, 2);
    TJS::TVPReadBuffer(stream, &encrypt, 2);
    TJS::TVPReadBuffer(stream, &offsetEncrypt, 4);
    TJS::TVPReadBuffer(stream, &offsetNames, 4);

    if (!memcmp(signature, "MDF", 3) || !memcmp(signature, "MFL", 3))
    {
        TVPConsoleLog("Maybe a MDF file");
        return false;
    }
    if (memcmp(signature, "PSB", 3))
    {
        TVPConsoleLog("Not a valid PSB file");
        return false;
    }
    if (offsetNames < stream->GetSize())
    {
        TJS::TVPReadBuffer(stream, &offsetStrings, 4);
        TJS::TVPReadBuffer(stream, &offsetStringsData, 4);
        TJS::TVPReadBuffer(stream, &offsetChunkOffsets, 4);
        TJS::TVPReadBuffer(stream, &offsetChunkLengths, 4);
        TJS::TVPReadBuffer(stream, &offsetChunkData, 4);
        TJS::TVPReadBuffer(stream, &offsetEntries, 4);

        if (version > 2)
        {
            TJS::TVPReadBuffer(stream, &checksum, 4);
        }
        if (version > 3)
        {
            TJS::TVPReadBuffer(stream, &offsetExtraChunkOffsets, 4);
            TJS::TVPReadBuffer(stream, &offsetExtraChunkLengths, 4);
            TJS::TVPReadBuffer(stream, &offsetExtraChunkData, 4);
        }
    }
    return true;
}

bool parsePSBArray(std::vector<tjs_uint32>* target, tjs_int8 n, TJS::tTJSBinaryStream* stream)
{
    if (n < 0 || n > 8)
    {
        TVPConsoleLog("bad length type size");
        return false;
    }
    tjs_uint32 count = 0;
    TJS::TVPReadBuffer(stream, &count, n);
    if (count > INT32_MAX)
    {
        TVPConsoleLog("Long array is not supported yet");
        return false;
    }
	tjs_uint8 entryType = 0;
	TJS::TVPReadBuffer(stream, &entryType, 1);
	tjs_uint32 entryLength = entryType - static_cast<tjs_uint32>(PSBObjType::NumberN8);
    target->reserve(count);
    for (int i = 0; i < count; i++)
    {
        tjs_uint32 result = 0;
        for (tjs_uint8 j = 0; j < entryLength; ++j)
        {
			tjs_uint8 value = 0;
			TJS::TVPReadBuffer(stream, &value, 1);
			result |= static_cast<tjs_uint32>(value) << (j * 8);
        }
        target->push_back(result);
    }
    return true;
}
} // namespace PSB



