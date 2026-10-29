# snoopyflyingace - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(snoopyflyingace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
    message(FATAL_ERROR
        "Game-derived sources are local only. Place your game data in game/ "
        "and run scripts/build.ps1 from the repository root before configuring CMake.")
endif()

include(generated/rexglue.cmake)

# Sources
set(SNOOPYFLYINGACE_SOURCES
    src/main.cpp
    src/local_input.cpp
    src/video.cpp
)

if(WIN32)
    add_executable(snoopyflyingace WIN32 ${SNOOPYFLYINGACE_SOURCES})
else()
    add_executable(snoopyflyingace ${SNOOPYFLYINGACE_SOURCES})
endif()

# Routing tests need SDK headers only, without loading game assets or the GPU.
option(REDBARON_BUILD_INPUT_TESTS "Build local device routing tests" OFF)
if(REDBARON_BUILD_INPUT_TESTS)
    enable_testing()
    add_executable(redbaron_input_tests tests/local_input_test.cpp)
    target_include_directories(redbaron_input_tests PRIVATE src
        $<TARGET_PROPERTY:rex::runtime,INTERFACE_INCLUDE_DIRECTORIES>)
    add_test(NAME redbaron_input_routing COMMAND redbaron_input_tests)
endif()

rexglue_setup_target(snoopyflyingace)

if(WIN32)
    add_executable(redbaron_probe src/launcher_probe.cpp src/video.cpp)
    target_link_libraries(redbaron_probe PRIVATE rex::runtime)
    # The opt-in FSR 3.1.5 SDK imports AMD's signed provider. Stage it for both
    # the game and the capability probe, including direct CMake builds.
    get_filename_component(REDBARON_SDK_ROOT "${rexglue_DIR}/../../.." ABSOLUTE)
    foreach(fsr_asset amd_fidelityfx_upscaler_dx12.dll FidelityFX-LICENSE.txt)
        set(fsr_source "${REDBARON_SDK_ROOT}/bin/${fsr_asset}")
        if(EXISTS "${fsr_source}")
            foreach(host snoopyflyingace redbaron_probe)
                add_custom_command(TARGET ${host} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${fsr_source}" "$<TARGET_FILE_DIR:${host}>" VERBATIM)
            endforeach()
        endif()
    endforeach()
endif()

# Stage controller mappings and a starter configuration beside the executable.
# Existing user settings must survive rebuilds.
if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/snoopyflyingace.toml")
    configure_file(config/snoopyflyingace.toml
        "${CMAKE_CURRENT_BINARY_DIR}/snoopyflyingace.toml" COPYONLY)
endif()
foreach(input_asset gamecontrollerdb.txt gamecontrollerdb.LICENSE.txt CONTROLES.txt)
    configure_file("config/${input_asset}"
        "${CMAKE_CURRENT_BINARY_DIR}/${input_asset}" COPYONLY)
endforeach()

# rexauto-pch: parse the SnoopyFlyingAce_init.h monolith once, not once per TU
# (build perf; output-neutral -- a PCH caches the AST, not emitted code).
target_precompile_headers(snoopyflyingace PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/SnoopyFlyingAce_init.h")

# rexauto-debug-diet: keep function symbols + line tables, drop the
# variable/type debug info that bloats the PDB and slows every relink
# (build perf; debug-info-only -- .text and codegen stay byte-identical).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(snoopyflyingace PRIVATE $<$<CONFIG:RelWithDebInfo>:-gline-tables-only>)
endif()
