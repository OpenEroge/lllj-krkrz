// xpck-patch: a translation patch shipped as an XPCK pack plus a Detours DLL
// whose behaviour is described by a config (e.g. lllj_zh.dll). The game
// profile passes that config, translated to TOML, to Install(); see
// games/lllj/zh-patch.toml for the format.
//
// What Install() sets up:
//   * the pack is mounted as xpck:// and its files win over every auto path
//     (engine storage leaf hook) -- the DLL did the same when opening files;
//   * redirect table: a listed bare name is replaced by its per-language name
//     on every lookup; the language comes from a TJS expression evaluated on
//     the main thread (other threads use the last value);
//   * movie subtitles and their fonts for the movie-ffmpeg module.
#pragma once
#include "tjsCommHead.h"
#include <vector>

namespace compat::xpck {

// Call from a compat::RegisterInit (the command line must be parsed).
void Install(const char *tomlConfig);
// The pack is mounted.
bool Enabled();
// The name the redirect table resolves leaf to now (leaf when unchanged).
ttstr Redirect(const ttstr &leaf);
// Read a file of the pack; false when it is not there.
bool ReadFile(const ttstr &leaf, std::vector<unsigned char> &data);

} // namespace compat::xpck
