cmake_minimum_required(VERSION 3.22)
# A library a target reaches twice is kept on the link line only where the linker needs it (CMake 3.29 on).
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()
# URL archives take the extraction time, so a changed dependency is newer than what was built from the one before.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Reckless UCI Host: a chess GUI and UCI engine host. The toolkit-free core: the rules of chess (src/chess), UCI
# engines as child processes (src/uci), games, matches and tournaments between them (src/match) and the
# command-line runner (src/cli). The GUI comes later, drawn with Skia on reckless-toolkit.

# The oldest macOS the Mac binaries run on, set before project(): 13.3, the floor every Reckless product shares
# (the toolkit the GUI will be built on writes numbers with the floating-point std::to_chars Apple's libc++ first
# has in 13.3).
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "The oldest macOS the Mac binaries run on" FORCE)
endif()

project(RecklessUciHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # the editor's code model reads build/<preset>/compile_commands.json

# cl and clang-cl both take MSVC's command line; every flag below is spelled for the front end, not the compiler.
set(UCI_HOST_MSVC_FRONTEND OFF)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(UCI_HOST_MSVC_FRONTEND ON)
endif()

if(MSVC)
    # The static release runtime, in every configuration: the binaries need no runtime installed beside them, and the
    # GUI's prebuilt Skia is built for it alone (the debug runtime's _ITERATOR_DEBUG_LEVEL 2 would not link against
    # it), so Debug keeps its /Od and /Zi on the release runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    add_compile_options(/utf-8)
    # cl's own parallel compilation inside one invocation. clang-cl takes /MP and then fails googletest's sources, and
    # Ninja runs a compiler per source anyway.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        add_compile_options(/MP)
    endif()
endif()

option(UCI_HOST_BUILD_TESTS "Build the test suites" ON)
option(UCI_HOST_COVERAGE "Instrument the host's own code for coverage (gcc, clang or clang-cl)" OFF)
option(UCI_HOST_ASAN "Build everything with AddressSanitizer (and UndefinedBehaviorSanitizer on gcc and clang)" OFF)

# The sanitizers on every target, GoogleTest's included, so a container passed between them is checked on both sides.
# MSVC's AddressSanitizer has no undefined-behaviour checker and wants no incremental link.
if(UCI_HOST_ASAN)
    if(UCI_HOST_MSVC_FRONTEND)
        add_compile_options(/fsanitize=address)
        add_link_options(/INCREMENTAL:NO)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# The strict warning set on every compiler, for the host's own targets. Not errors: a new compiler must not break the
# build; the code is kept warning-free on gcc, clang and MSVC, and CI's logs are the check. clang-cl reads -Wall as
# clang's -Weverything, so it takes MSVC's /W4 (clang's -Wall -Wextra) as cl does.
add_library(uci_host_warnings INTERFACE)
if(UCI_HOST_MSVC_FRONTEND)
    target_compile_options(uci_host_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(uci_host_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Coverage of the host's own code alone: the flags ride on uci_host_warnings, which every target of the host links and
# GoogleTest does not. clang and clang-cl count with LLVM's source-based coverage, read by llvm-cov
# (tools/coverage-windows.ps1 on Windows): gcov's notes under clang-cl skip every function that can throw. clang-cl
# links through the MSVC linker, which takes no compiler flag: the objects name clang's profile runtime themselves, and
# the linker finds it under clang's resource directory.
if(UCI_HOST_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(uci_host_warnings INTERFACE --coverage -O0)
        target_link_options(uci_host_warnings INTERFACE --coverage)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND UCI_HOST_MSVC_FRONTEND)
        target_compile_options(uci_host_warnings INTERFACE -fprofile-instr-generate -fcoverage-mapping /Od)
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /clang:-print-resource-dir
                        OUTPUT_VARIABLE clang_resource_dir OUTPUT_STRIP_TRAILING_WHITESPACE)
        file(TO_CMAKE_PATH "${clang_resource_dir}" clang_resource_dir)
        if(NOT EXISTS "${clang_resource_dir}/lib/windows")
            message(FATAL_ERROR "clang-cl's profile runtime is not under ${clang_resource_dir}/lib/windows")
        endif()
        target_link_directories(uci_host_warnings INTERFACE "${clang_resource_dir}/lib/windows")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(uci_host_warnings INTERFACE -fprofile-instr-generate -fcoverage-mapping -O0)
        target_link_options(uci_host_warnings INTERFACE -fprofile-instr-generate)
    else()
        message(FATAL_ERROR "UCI_HOST_COVERAGE needs gcc, clang or clang-cl, not ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(UciHostVersion)

if(UCI_HOST_BUILD_TESTS)
    enable_testing()
    # GoogleTest, pinned by its release tarball's hash. gtest alone: nothing here mocks.
    include(FetchContent)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL      https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
        URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c)
    FetchContent_MakeAvailable(googletest)
    # googletest compiles its own two libraries under its own strict set (-Wall -Wconversion and /WX on clang-cl),
    # where clang 21's -Wcharacter-conversion meets its char8_t printer in gtest-printers.h. That is googletest's to
    # fix, so those two libraries alone let it pass; the suites see its headers as system headers already.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag(-Wcharacter-conversion UCI_HOST_CLANG_KNOWS_CHARACTER_CONVERSION)
        if(UCI_HOST_CLANG_KNOWS_CHARACTER_CONVERSION)
            target_compile_options(gtest PRIVATE -Wno-character-conversion)
            target_compile_options(gtest_main PRIVATE -Wno-character-conversion)
        endif()
    endif()
endif()

add_subdirectory(src)
if(UCI_HOST_BUILD_TESTS)
    add_subdirectory(tests)
endif()
