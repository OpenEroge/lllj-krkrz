set(TPSTUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tp_stub CACHE PATH "Path to tp_stub directory")
include(${TPSTUB_DIR}/krkrz.cmake)

# 追加トランジション (wave / mosaic / turn / rotate / ripple) プラグイン。
#
# トランジションハンドラプロバイダ機構 (TVPAddTransHandlerProvider /
# common/visual/TransIntf) は全バリアント共通なので generic 版でも動作する。
# V2Link エントリは simplebinder/v2link.cpp のポータブル scaffold を利用し、
# 本体側は onV2Link / onV2Unlink (Main.cpp) を提供する。
krkrz_plugin(extrans
    SOURCES
        Main.cpp
        wave.cpp
        mosaic.cpp
        turn.cpp
        turntrans_table.cpp
        rotatebase.cpp
        rotatetrans.cpp
        ripple.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../simplebinder/v2link.cpp
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../simplebinder
)
