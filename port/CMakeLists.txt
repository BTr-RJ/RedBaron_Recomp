# snoopyflyingace - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(snoopyflyingace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(SNOOPYFLYINGACE_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(snoopyflyingace WIN32 ${SNOOPYFLYINGACE_SOURCES})
else()
    add_executable(snoopyflyingace ${SNOOPYFLYINGACE_SOURCES})
endif()

rexglue_setup_target(snoopyflyingace)

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
