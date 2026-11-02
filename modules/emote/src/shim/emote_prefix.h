#pragma once
// PORT: force-included into every E-mote source (see CMakeLists.txt). Stands in
// for krkr-ns' precompiled header (SDLPrecompiledHeader.h), which made
// tjsCommHead.h and the usual standard headers implicitly available, and for
// the engine headers its include graph happened to provide.
#include "tjsCommHead.h"
#include <algorithm>
#include <cstring>
#include <string>
#include "LayerImpl.h" // tTJSNI_Layer (SeparateLayerAdaptor)
