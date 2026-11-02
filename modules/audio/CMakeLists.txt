# audio: WaveSoundBuffer filters (wfTypicalDSP / wfBasicEffect: WaveDSPFilter,
# DelayEffect, StkFreeVerb) and multi-track BGM (.mchx, wumultitrack), on
# DSPFilters (third_party/DSPFilters, MIT).
file(GLOB _dsp CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/DSPFilters/source/*.cpp)
file(GLOB _src CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
compat_module(audio
    SOURCES ${_src} ${_dsp}
    PRELUDE prelude/audio.tjs
    PRIVATE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/DSPFilters/include)
