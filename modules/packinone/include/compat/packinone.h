// packinone module: public helpers for other modules and game profiles.
#pragma once
#include "tjsCommHead.h"

namespace compat {
// Hxv4 archive name of a file: BLAKE2s-256 of the lower-case leaf name followed
// by the archive's domain string (UTF-16LE), as 64 upper-case hex digits.
// Unpacked Hxv4 archives keep files whose real name is unknown under it.
ttstr HxFilename(const ttstr &leaf, const ttstr &domain);
} // namespace compat
