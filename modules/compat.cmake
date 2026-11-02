# Compatibility modules: CMake helpers.
#
# A module is a directory under modules/ with a CMakeLists.txt that calls
# compat_module(). Its sources are compiled into the engine executable (not a
# separate library), so they can use engine-internal headers and their static
# registration objects (compat/compat.h) are always linked.
#
# A game profile (games/<name>/CMakeLists.txt) selects modules with
# compat_use(), declares its own sources with compat_module(), and finally
# calls compat_export() to hand everything to the engine build.
#
#   compat_module(<name>
#       [SOURCES files...]          default: src/*.cpp and src/*.c
#       [PRELUDE files...]          TJS run at startup, in this order
#       [ORDER n]                   prelude order (compat.h Order*), default 100
#       [INCLUDE dirs...]           visible to every module (public headers)
#       [PRIVATE_INCLUDE dirs...]   this module's sources only
#       [DEFINITIONS defs...]       this module's sources only
#       [OPTIONS opts...]           compile options, this module's sources only
#       [LIBS libs...])             linked into the executable
#
#   compat_use(<module>...)         add modules (and nothing twice)
#   compat_embed_text(<file> <symbol> <out.inc>)
#                                   static const char <symbol>[] = file contents
#   compat_export()                 macro, call last in the game profile

set(COMPAT_MODULES_DIR ${CMAKE_CURRENT_LIST_DIR})

# Write content to file only when it changed (keeps rebuilds incremental).
function(_compat_write_if_changed file content)
    if(EXISTS "${file}")
        file(READ "${file}" _old)
        if(_old STREQUAL content)
            return()
        endif()
    endif()
    file(WRITE "${file}" "${content}")
endfunction()

# Raw string literal of a text file. Re-run configure when the file changes.
function(_compat_raw_literal file prefix out)
    file(READ "${file}" _text)
    string(FIND "${_text}" ")COMPAT_TEXT\"" _clash)
    if(NOT _clash EQUAL -1)
        message(FATAL_ERROR "${file} contains the literal delimiter )COMPAT_TEXT\"")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
    set(${out} "${prefix}R\"COMPAT_TEXT(${_text})COMPAT_TEXT\"" PARENT_SCOPE)
endfunction()

function(compat_embed_text file symbol out)
    _compat_raw_literal("${file}" "" _lit)
    _compat_write_if_changed("${out}" "static const char ${symbol}[] = ${_lit};\n")
endfunction()

function(compat_module name)
    cmake_parse_arguments(M "" "ORDER"
        "SOURCES;PRELUDE;INCLUDE;PRIVATE_INCLUDE;DEFINITIONS;OPTIONS;LIBS" ${ARGN})
    if(NOT DEFINED M_ORDER)
        set(M_ORDER 100)
    endif()
    if(NOT M_SOURCES)
        file(GLOB M_SOURCES CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
    endif()

    # Each prelude file becomes a small source registering it with compat-core.
    set(_index 0)
    foreach(_tjs IN LISTS M_PRELUDE)
        get_filename_component(_tjs "${_tjs}" ABSOLUTE)
        get_filename_component(_base "${_tjs}" NAME)
        _compat_raw_literal("${_tjs}" "u" _lit)
        math(EXPR _order "${M_ORDER} + ${_index}")
        set(_gen "${CMAKE_CURRENT_BINARY_DIR}/prelude_${_index}_${_base}.cpp")
        _compat_write_if_changed("${_gen}"
"// generated from ${_tjs}\n#include \"compat/compat.h\"\nstatic const char16_t text[] = ${_lit};\nstatic compat::RegisterPrelude reg(${_order}, \"${name}/${_base}\", text);\n")
        list(APPEND M_SOURCES "${_gen}")
        math(EXPR _index "${_index} + 1")
    endforeach()

    if(M_PRIVATE_INCLUDE)
        set_property(SOURCE ${M_SOURCES} DIRECTORY ${KRKRZ_CORE_DIR}
            APPEND PROPERTY INCLUDE_DIRECTORIES ${M_PRIVATE_INCLUDE})
    endif()
    if(M_DEFINITIONS)
        set_property(SOURCE ${M_SOURCES} DIRECTORY ${KRKRZ_CORE_DIR}
            APPEND PROPERTY COMPILE_DEFINITIONS ${M_DEFINITIONS})
    endif()
    if(M_OPTIONS)
        set_property(SOURCE ${M_SOURCES} DIRECTORY ${KRKRZ_CORE_DIR}
            APPEND PROPERTY COMPILE_OPTIONS ${M_OPTIONS})
    endif()
    set_property(GLOBAL APPEND PROPERTY COMPAT_SOURCES ${M_SOURCES})
    set_property(GLOBAL APPEND PROPERTY COMPAT_INCLUDES ${M_INCLUDE})
    set_property(GLOBAL APPEND PROPERTY COMPAT_LIBS ${M_LIBS})
    set_property(GLOBAL APPEND PROPERTY COMPAT_MODULES ${name})
endfunction()

function(compat_use)
    get_property(_have GLOBAL PROPERTY COMPAT_MODULES_ADDED)
    foreach(_m IN LISTS ARGN)
        if(NOT _m IN_LIST _have)
            if(NOT EXISTS "${COMPAT_MODULES_DIR}/${_m}/CMakeLists.txt")
                message(FATAL_ERROR "compat module '${_m}' not found in ${COMPAT_MODULES_DIR}")
            endif()
            add_subdirectory("${COMPAT_MODULES_DIR}/${_m}" "${CMAKE_BINARY_DIR}/compat/${_m}")
            list(APPEND _have ${_m})
            set_property(GLOBAL PROPERTY COMPAT_MODULES_ADDED ${_have})
        endif()
    endforeach()
endfunction()

# The engine adds the game profile like a static plugin (TVP_PLUGINS) and reads
# KRKRZ_SRC_ADD / KRKRZ_LIB_ADD / KRKRZ_INC back from it, hence a macro.
macro(compat_export)
    get_property(_compat_src GLOBAL PROPERTY COMPAT_SOURCES)
    get_property(_compat_inc GLOBAL PROPERTY COMPAT_INCLUDES)
    get_property(_compat_libs GLOBAL PROPERTY COMPAT_LIBS)
    get_property(_compat_mods GLOBAL PROPERTY COMPAT_MODULES)
    message(STATUS "compat modules: ${_compat_mods}")
    set(KRKRZ_SRC_ADD ${KRKRZ_SRC_ADD} ${_compat_src} PARENT_SCOPE)
    set(KRKRZ_INC ${KRKRZ_INC} ${_compat_inc} PARENT_SCOPE)
    set(KRKRZ_LIB_ADD ${KRKRZ_LIB_ADD} ${_compat_libs} PARENT_SCOPE)
    # FFmpeg and SDL are found next to the executable
    set(CMAKE_BUILD_RPATH ${CMAKE_BUILD_RPATH} "\$ORIGIN" PARENT_SCOPE)
endmacro()
