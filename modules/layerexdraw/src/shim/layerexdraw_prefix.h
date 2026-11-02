#pragma once
// PORT: force-included into every layerExDraw source (see CMakeLists.txt). It
// stands in for the ncbind include the krkr-ns sources start with (they then
// include "ncbind/ncbind.hpp" themselves, which is a no-op after this), and for
// the engine headers krkr-ns' precompiled header provided.
#define TVP_PLUGIN_NAME compat_layerexdraw
#include "compat/ncbind.h"
#include "LayerImpl.h" // tTJSNI_Layer (layerExBase_GL)
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
