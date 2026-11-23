cmake_minimum_required(VERSION 3.21)
project(MarkdownNotes VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MN_BUILD_TESTS "Build tests" ON)
option(MN_BUILD_BENCH "Build benchmarks" ON)
option(MN_SANITIZE "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
if(MN_BUILD_TESTS OR MN_BUILD_BENCH)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()
qt_standard_project_setup()

if(MN_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(third_party)

# Warning flags for first-party code only.
add_library(mn_warnings INTERFACE)
if(MSVC)
    target_compile_options(mn_warnings INTERFACE /W4 /permissive- /utf-8)
    if(MN_WARNINGS_AS_ERRORS)
        target_compile_options(mn_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(mn_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(MN_WARNINGS_AS_ERRORS)
        target_compile_options(mn_warnings INTERFACE -Werror)
    endif()
endif()
target_compile_definitions(mn_warnings INTERFACE
    QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII QT_NO_URL_CAST_FROM_STRING
    QT_NO_CAST_FROM_BYTEARRAY QT_USE_QSTRINGBUILDER)

add_subdirectory(src)

if(MN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(MN_BUILD_BENCH)
    add_subdirectory(bench)
endif()
