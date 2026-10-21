cmake_minimum_required(VERSION 3.22)
# A library a target reaches twice is kept on the link line only where the linker needs it (CMake 3.29 on).
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()
# URL archives take the extraction time, so a changed dependency is newer than what was built from the one before.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Reckless Amphitheater: a chess GUI and UCI engine host. The toolkit-free core: the rules of chess (src/chess), UCI
# engines as child processes (src/uci), games, matches and tournaments between them (src/match) and the
# command-line runner (src/cli) - and the window, Reckless Amphitheater (src/app), drawn with Skia on reckless-toolkit.

# The oldest macOS the Mac binaries run on, set before project(): 13.3, the floor every Reckless product shares
# (the toolkit the GUI will be built on writes numbers with the floating-point std::to_chars Apple's libc++ first
# has in 13.3).
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "The oldest macOS the Mac binaries run on" FORCE)
endif()

project(Amphitheater LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # the editor's code model reads build/<preset>/compile_commands.json

# cl and clang-cl both take MSVC's command line; every flag below is spelled for the front end, not the compiler.
set(AMPHITHEATER_MSVC_FRONTEND OFF)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(AMPHITHEATER_MSVC_FRONTEND ON)
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

option(AMPHITHEATER_BUILD_TESTS "Build the test suites" ON)
option(AMPHITHEATER_GUI "Build the window, Reckless Amphitheater, on the toolkit submodule (external/reckless-toolkit)"
       ON)
option(AMPHITHEATER_COVERAGE "Instrument the host's own code for coverage (gcc, clang or clang-cl)" OFF)
option(AMPHITHEATER_ASAN "Build everything with AddressSanitizer (and UndefinedBehaviorSanitizer on gcc and clang)" OFF)

# The sanitizers on every target, GoogleTest's included, so a container passed between them is checked on both sides.
# MSVC's AddressSanitizer has no undefined-behaviour checker and wants no incremental link.
if(AMPHITHEATER_ASAN)
    if(AMPHITHEATER_MSVC_FRONTEND)
        add_compile_options(/fsanitize=address)
        add_link_options(/INCREMENTAL:NO)
    else()
        # UndefinedBehaviorSanitizer's vptr check reads the type information of every polymorphic class a checked call
        # reaches, and the window's prebuilt Skia is built without it (-fno-rtti): the check is left out, the rest kept.
        add_compile_options(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined -fno-sanitize=vptr)
    endif()
endif()

# The strict warning set on every compiler, for the host's own targets. Not errors: a new compiler must not break the
# build; the code is kept warning-free on gcc, clang and MSVC, and CI's logs are the check. clang-cl reads -Wall as
# clang's -Weverything, so it takes MSVC's /W4 (clang's -Wall -Wextra) as cl does.
add_library(amphitheater_warnings INTERFACE)
if(AMPHITHEATER_MSVC_FRONTEND)
    target_compile_options(amphitheater_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(amphitheater_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Coverage of the host's own code alone: the flags ride on amphitheater_warnings, which every target of the host links
# and GoogleTest does not. clang and clang-cl count with LLVM's source-based coverage, read by llvm-cov
# (tools/coverage-windows.ps1 on Windows): gcov's notes under clang-cl skip every function that can throw. clang-cl
# links through the MSVC linker, which takes no compiler flag: the objects name clang's profile runtime themselves, and
# the linker finds it under clang's resource directory.
if(AMPHITHEATER_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(amphitheater_warnings INTERFACE --coverage -O0)
        target_link_options(amphitheater_warnings INTERFACE --coverage)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND AMPHITHEATER_MSVC_FRONTEND)
        target_compile_options(amphitheater_warnings INTERFACE -fprofile-instr-generate -fcoverage-mapping /Od)
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /clang:-print-resource-dir
                        OUTPUT_VARIABLE clang_resource_dir OUTPUT_STRIP_TRAILING_WHITESPACE)
        file(TO_CMAKE_PATH "${clang_resource_dir}" clang_resource_dir)
        if(NOT EXISTS "${clang_resource_dir}/lib/windows")
            message(FATAL_ERROR "clang-cl's profile runtime is not under ${clang_resource_dir}/lib/windows")
        endif()
        target_link_directories(amphitheater_warnings INTERFACE "${clang_resource_dir}/lib/windows")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(amphitheater_warnings INTERFACE -fprofile-instr-generate -fcoverage-mapping -O0)
        target_link_options(amphitheater_warnings INTERFACE -fprofile-instr-generate)
    else()
        message(FATAL_ERROR "AMPHITHEATER_COVERAGE needs gcc, clang or clang-cl, not ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AmphitheaterVersion)

if(AMPHITHEATER_BUILD_TESTS)
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
        check_cxx_compiler_flag(-Wcharacter-conversion AMPHITHEATER_CLANG_KNOWS_CHARACTER_CONVERSION)
        if(AMPHITHEATER_CLANG_KNOWS_CHARACTER_CONVERSION)
            target_compile_options(gtest PRIVATE -Wno-character-conversion)
            target_compile_options(gtest_main PRIVATE -Wno-character-conversion)
        endif()
    endif()
endif()

# The Python the suites run the packaging and notices tests with: the repository's own .venv where there is one (made
# by the setup scripts), else the first Python 3 on the PATH; -DPython3_EXECUTABLE names one outright (CI passes a venv
# of its own). The build itself needs none: the app's icon is rendered by tools/make_icon.py and committed.
if(NOT Python3_EXECUTABLE)
    foreach(_candidate "${CMAKE_CURRENT_SOURCE_DIR}/.venv/Scripts/python.exe" "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python")
        if(EXISTS "${_candidate}")
            set(Python3_EXECUTABLE "${_candidate}" CACHE FILEPATH "The Python the suites run their scripts with")
            break()
        endif()
    endforeach()
endif()

# The window, Reckless Amphitheater, is drawn on the toolkit (reckless-technology/reckless-toolkit), a git submodule at
# external/reckless-toolkit: its views, its look, its child window on Skia and its app platform. The core and the
# command line need none of it, and build with the GUI off.
if(AMPHITHEATER_GUI)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/reckless-toolkit/CMakeLists.txt")
        message(FATAL_ERROR "The window is built on the toolkit, a git submodule at external/reckless-toolkit, which this "
                            "checkout does not hold. Fetch it with 'git submodule update --init' (tools/setup-dev.ps1 "
                            "and tools/setup-dev.sh do), or build the core and the command line alone with "
                            "-DAMPHITHEATER_GUI=OFF.")
    endif()

    # The toolkit's options, as normal variables before it is added: its windowed parts on, its own suites off (the
    # host's suites are its own), and no audio device layer - the host plays no sound.
    set(RECKLESS_TOOLKIT_BUILD_TESTS OFF)
    set(RECKLESS_TOOLKIT_GUI ON)
    set(RECKLESS_BUILD_AUDIO OFF)
    set(RECKLESS_ASIO OFF)
    add_subdirectory(external/reckless-toolkit EXCLUDE_FROM_ALL)
endif()

add_subdirectory(src)
if(AMPHITHEATER_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# The default target is the app: a plain build (`make`, `cmake --build build`) builds Reckless Amphitheater and what it
# links, and nothing else - with AMPHITHEATER_GUI off, the command line in its place. amphitheater_everything builds
# every target: the command line, the render tool, the suites and the fake engine. The presets, CI and the release
# build it, and CTest builds it before it runs a test (tests/CMakeLists.txt), so a plain build and then ctest work.
function(amphitheater_collect_targets directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    set_property(GLOBAL APPEND PROPERTY AMPHITHEATER_TARGETS ${targets})
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        amphitheater_collect_targets("${subdirectory}")
    endforeach()
endfunction()
get_property(top_level_targets DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
set_property(GLOBAL PROPERTY AMPHITHEATER_TARGETS ${top_level_targets})
foreach(directory src tests)
    if(EXISTS "${PROJECT_SOURCE_DIR}/${directory}/CMakeLists.txt" AND (directory STREQUAL "src" OR AMPHITHEATER_BUILD_TESTS))
        amphitheater_collect_targets("${PROJECT_SOURCE_DIR}/${directory}")
    endif()
endforeach()
get_property(project_targets GLOBAL PROPERTY AMPHITHEATER_TARGETS)
foreach(target IN ITEMS gtest gtest_main)
    if(TARGET ${target})
        list(APPEND project_targets ${target})
    endif()
endforeach()
if(TARGET Amphitheater)
    set(default_target Amphitheater)
else()
    set(default_target amphitheater-cli)
endif()
add_custom_target(amphitheater_everything)
foreach(target IN LISTS project_targets)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "INTERFACE_LIBRARY" OR type STREQUAL "UTILITY")
        continue()
    endif()
    add_dependencies(amphitheater_everything ${target})
    if(NOT target STREQUAL default_target)
        set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endforeach()
