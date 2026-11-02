# packinone: PackinOne.dll -- tjsDataPack saves (TJS/4s0, TJS/ns0), the
# loose natives games call without guards, and the Hxv4 file name hash.
compat_module(packinone
    PRELUDE prelude/packinone.tjs
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)
