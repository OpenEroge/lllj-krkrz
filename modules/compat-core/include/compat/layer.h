// Small Layer helpers shared by modules that implement Layer members.
#pragma once
#include "tjsCommHead.h"

class tTJSNI_BaseLayer;

namespace compat {
// The engine's layer behind a TJS Layer object, or null for anything else.
tTJSNI_BaseLayer *LayerOf(tTJSVariant *v);
} // namespace compat

// Size of an image as Layer.loadImages would load it (same name resolution:
// auto path, missing extension). False when it cannot be loaded. The name and
// signature are krkrsdl3's engine function, which ported code expects.
bool Layer_FetchImageSize(ttstr imageName, int &width, int &height);
