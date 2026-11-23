# Vendored third-party code. Compiled without the project's warning flags.

# --- md4c (MIT) -------------------------------------------------------------
add_library(md4c STATIC md4c/md4c.c)
target_include_directories(md4c SYSTEM PUBLIC md4c)
# Local patch (see md4c/wikilink-limit.patch): raise the 100-char wikilink
# destination limit, because Obsidian has none.
target_compile_definitions(md4c PRIVATE MD4C_WIKILINK_MAX_DEST=4096)
set_target_properties(md4c PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

# --- SQLite amalgamation (public domain) ------------------------------------
add_library(sqlite3 STATIC sqlite/sqlite3.c)
target_include_directories(sqlite3 SYSTEM PUBLIC sqlite)
target_compile_definitions(sqlite3
    PRIVATE
        SQLITE_ENABLE_FTS5
        SQLITE_THREADSAFE=2          # one connection per thread
        SQLITE_DQS=0
        SQLITE_DEFAULT_MEMSTATUS=0
        SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
        SQLITE_LIKE_DOESNT_MATCH_BLOBS
        SQLITE_MAX_EXPR_DEPTH=0
        SQLITE_OMIT_DEPRECATED
        SQLITE_OMIT_LOAD_EXTENSION
        SQLITE_OMIT_SHARED_CACHE
        SQLITE_USE_ALLOCA
)
find_package(Threads REQUIRED)
target_link_libraries(sqlite3 PRIVATE Threads::Threads)
if(UNIX)
    target_link_libraries(sqlite3 PRIVATE m)
endif()

# --- MicroTeX (MIT) ---------------------------------------------------------
file(GLOB_RECURSE MICROTEX_SOURCES CONFIGURE_DEPENDS
    microtex/src/atom/*.cpp microtex/src/box/*.cpp microtex/src/core/*.cpp
    microtex/src/fonts/*.cpp microtex/src/utils/*.cpp microtex/src/res/*.cpp
    microtex/src/graphic/*.cpp)
add_library(microtex STATIC
    ${MICROTEX_SOURCES}
    microtex/src/latex.cpp
    microtex/src/render.cpp
    microtex/src/platform/qt/graphic_qt.cpp
    microtex/shim/tinyxml2_shim.cpp)
target_include_directories(microtex SYSTEM PUBLIC microtex/src microtex/shim)
target_compile_definitions(microtex PUBLIC BUILD_QT PRIVATE NDEBUG)
target_link_libraries(microtex PUBLIC Qt6::Core Qt6::Gui)
set_target_properties(microtex PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(microtex PRIVATE /utf-8 /w)
else()
    target_compile_options(microtex PRIVATE -w)
endif()

# Fonts and alphabet descriptions, embedded as a Qt resource under :/microtex/res
file(GLOB_RECURSE MICROTEX_RES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    CONFIGURE_DEPENDS microtex/res/*.ttf microtex/res/*.xml)
add_library(microtex_res OBJECT)
set_target_properties(microtex_res PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(microtex_res PRIVATE Qt6::Core)
qt_add_resources(microtex_res "microtex_res"
    PREFIX "/microtex"
    BASE microtex
    FILES ${MICROTEX_RES})
