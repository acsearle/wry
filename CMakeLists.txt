# wry: the CMake build.
#
# This file is the build's description of record; the Xcode project under
# client/ is the other consumer of the same source folders and is kept
# until a generated one has earned trust (docs/platforms.md section 2).
#
# Membership is folder-based, as in the Xcode project's synchronized
# groups: every .cpp (and, on Apple, every .mm) under the six module
# folders is compiled.  Adding a file needs no edit here; the
# CONFIGURE_DEPENDS globs re-run at the next build.
#
#   cmake --preset mac-debug && cmake --build --preset mac-debug
#   ctest --preset mac-debug            # the suite, client --test-only
#
# The flags below reproduce what the Xcode scheme passes (captured from
# an xcodebuild log, 2026-10-26): C++23, no extensions, the scheme's
# warning set, -O0 with DEBUG=1 and libc++'s debug hardening in Debug,
# -Os with NDEBUG=1 in Release, Address + Undefined sanitizers unless a
# preset says otherwise, and the Metal compiler's fast-math flags.

cmake_minimum_required(VERSION 3.28)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum macOS version")
endif()

project(wry LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Per-configuration flags, replacing CMake's defaults (-O3 in Release,
# plain -g in Debug) with the Xcode project's.
foreach(lang CXX OBJCXX)
    set(CMAKE_${lang}_FLAGS_DEBUG "-O0 -g")
    set(CMAKE_${lang}_FLAGS_RELEASE "-Os -g")
    set(CMAKE_${lang}_FLAGS_RELWITHDEBINFO "-O2 -g")
    set(CMAKE_${lang}_FLAGS_MINSIZEREL "-Os -g")
endforeach()

# What the Xcode scheme's Diagnostics tab chooses at launch.  "address"
# is the scheme's default (Address + Undefined); "thread" is the variant
# the GC and reactor work runs under (Thread + Undefined); "none" is a
# plain build for real timings.
set(WRY_SANITIZER "address" CACHE STRING "Sanitizer set: address, thread or none")
set_property(CACHE WRY_SANITIZER PROPERTY STRINGS address thread none)

set(WRY_SRC ${CMAKE_SOURCE_DIR}/client/client)
set(WRY_MODULES core container io game render ext)

set(WRY_GLOBS "")
set(WRY_OBJCXX_GLOBS "")
foreach(module ${WRY_MODULES})
    list(APPEND WRY_GLOBS ${WRY_SRC}/${module}/*.cpp)
    list(APPEND WRY_OBJCXX_GLOBS ${WRY_SRC}/${module}/*.mm)
endforeach()
file(GLOB_RECURSE WRY_CXX_SOURCES CONFIGURE_DEPENDS ${WRY_GLOBS})
set(WRY_OBJCXX_SOURCES "")
if(APPLE)
    file(GLOB_RECURSE WRY_OBJCXX_SOURCES CONFIGURE_DEPENDS ${WRY_OBJCXX_GLOBS})
endif()
file(GLOB_RECURSE WRY_METAL_SOURCES CONFIGURE_DEPENDS ${WRY_SRC}/render/*.metal)
file(GLOB_RECURSE WRY_METAL_HEADERS CONFIGURE_DEPENDS ${WRY_SRC}/render/*.h)

add_executable(client MACOSX_BUNDLE ${WRY_CXX_SOURCES} ${WRY_OBJCXX_SOURCES})

# Headers are included by bare name across the module folders (the Xcode
# project resolves them through its header map), so every folder is an
# include directory.
foreach(module ${WRY_MODULES})
    target_include_directories(client PRIVATE ${WRY_SRC}/${module})
endforeach()

target_compile_definitions(client PRIVATE
    $<$<CONFIG:Debug>:DEBUG=1>
    $<$<CONFIG:Debug>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>
)

target_compile_options(client PRIVATE
    -fno-common
    -fvisibility-inlines-hidden
    -fstrict-aliasing
    # The scheme's warning set, verbatim
    -Wno-trigraphs -Wno-missing-field-initializers -Wno-missing-prototypes
    -Werror=return-type -Wdocumentation -Wunreachable-code
    -Wquoted-include-in-framework-header
    -Wno-non-virtual-dtor -Wno-overloaded-virtual -Wno-exit-time-destructors
    -Wno-missing-braces -Wparentheses -Wswitch -Wunused-function
    -Wno-unused-label -Wno-unused-parameter -Wunused-variable -Wunused-value
    -Wempty-body -Wuninitialized -Wconditional-uninitialized
    -Wno-unknown-pragmas -Wno-shadow -Wno-four-char-constants -Wno-conversion
    -Wconstant-conversion -Wint-conversion -Wbool-conversion -Wenum-conversion
    -Wno-float-conversion -Wnon-literal-null-conversion -Wshorten-64-to-32
    -Wno-newline-eof -Wno-c++11-extensions -Watomic-implicit-seq-cst
    -Wno-implicit-fallthrough -Wdeprecated-declarations -Winvalid-offsetof
    -Wno-sign-conversion -Winfinite-recursion -Wmove -Wcomma
    -Wstrict-prototypes -Wrange-loop-analysis -Wunguarded-availability
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-Werror=deprecated-objc-isa-usage>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-Werror=objc-root-class>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-Wobjc-literal-conversion>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-Wblock-capture-autoreleasing>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-Wno-arc-repeated-use-of-weak>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-Wno-objc-interface-ivars>
)

# Sanitizers: the flag set the scheme produces, including its exclusions
set(WRY_UBSAN_FLAGS
    -fsanitize=undefined
    -fno-sanitize=enum,return,float-divide-by-zero,function,vptr
    -fsanitize=nullability)
if(WRY_SANITIZER STREQUAL "address")
    set(WRY_SANITIZER_FLAGS -fsanitize=address ${WRY_UBSAN_FLAGS})
    target_compile_definitions(client PRIVATE __SANITIZER_DISABLE_CONTAINER_OVERFLOW__)
elseif(WRY_SANITIZER STREQUAL "thread")
    set(WRY_SANITIZER_FLAGS -fsanitize=thread ${WRY_UBSAN_FLAGS})
elseif(WRY_SANITIZER STREQUAL "none")
    set(WRY_SANITIZER_FLAGS "")
else()
    message(FATAL_ERROR "WRY_SANITIZER must be address, thread or none, not '${WRY_SANITIZER}'")
endif()
target_compile_options(client PRIVATE ${WRY_SANITIZER_FLAGS})
target_link_options(client PRIVATE ${WRY_SANITIZER_FLAGS})

# Third-party libraries, found where the Xcode project hard-wires them
find_package(PNG REQUIRED)
find_package(Freetype REQUIRED)
find_package(SQLite3 REQUIRED)
target_link_libraries(client PRIVATE PNG::PNG Freetype::Freetype SQLite3::SQLite3)

if(APPLE)
    set_target_properties(client PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER surly.client
        MACOSX_BUNDLE_BUNDLE_NAME client
        MACOSX_BUNDLE_SHORT_VERSION_STRING 1.0
        MACOSX_BUNDLE_BUNDLE_VERSION 1
    )
    target_link_options(client PRIVATE
        -fobjc-arc -fobjc-link-runtime
        -Wl,-dead_strip -rdynamic
        -Wl,-no_warn_duplicate_libraries)
    foreach(framework AppKit AVFAudio AVFoundation CoreMedia CoreVideo Foundation
                      GameController Metal MetalKit MetalPerformanceShaders QuartzCore)
        target_link_libraries(client PRIVATE "-framework ${framework}")
    endforeach()
    # The shader library: every .metal under render/ compiled to AIR and
    # linked into Contents/Resources/default.metallib, which
    # WryRenderContext loads as the default library.  Debug keeps line
    # tables and sources for the GPU debugger, as MTL_ENABLE_DEBUG_INFO =
    # INCLUDE_SOURCE does in Xcode.
    set(WRY_METAL_TARGET -target air64-apple-macos${CMAKE_OSX_DEPLOYMENT_TARGET})
    set(WRY_METAL_DEBUG $<$<CONFIG:Debug>:-gline-tables-only> $<$<CONFIG:Debug>:-frecord-sources>)
    set(WRY_AIR_FILES "")
    foreach(shader ${WRY_METAL_SOURCES})
        get_filename_component(name ${shader} NAME_WE)
        set(air ${CMAKE_CURRENT_BINARY_DIR}/metal/${name}.air)
        add_custom_command(
            OUTPUT ${air}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/metal
            COMMAND xcrun -sdk macosx metal ${WRY_METAL_TARGET} ${WRY_METAL_DEBUG}
                    -fmetal-math-mode=fast -fmetal-math-fp32-functions=fast
                    -c ${shader} -o ${air}
            DEPENDS ${shader} ${WRY_METAL_HEADERS}
            COMMAND_EXPAND_LISTS
            COMMENT "Compiling Metal shader ${name}.metal"
            VERBATIM)
        list(APPEND WRY_AIR_FILES ${air})
    endforeach()
    set(WRY_METALLIB ${CMAKE_CURRENT_BINARY_DIR}/metal/default.metallib)
    add_custom_command(
        OUTPUT ${WRY_METALLIB}
        COMMAND xcrun -sdk macosx metal ${WRY_METAL_TARGET}
                $<$<CONFIG:Debug>:-frecord-sources>
                -o ${WRY_METALLIB} ${WRY_AIR_FILES}
        DEPENDS ${WRY_AIR_FILES}
        COMMAND_EXPAND_LISTS
        COMMENT "Linking default.metallib"
        VERBATIM)
    target_sources(client PRIVATE ${WRY_METALLIB})
    set_source_files_properties(${WRY_METALLIB} PROPERTIES
        GENERATED TRUE
        MACOSX_PACKAGE_LOCATION Resources)
endif()

# The suite: the binary's own headless test runner
enable_testing()
add_test(NAME suite COMMAND client --test-only)
set_tests_properties(suite PROPERTIES
    TIMEOUT 900
    ENVIRONMENT "SWIFT_BACKTRACE=enable=no")
