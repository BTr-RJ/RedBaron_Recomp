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
    src/branding.cpp
    src/online.cpp
)
add_executable(redbaron_achievement_export src/achievement_export.cpp)
target_link_libraries(redbaron_achievement_export PRIVATE rex::runtime)
add_custom_command(TARGET redbaron_achievement_export POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:redbaron_achievement_export>")

if(WIN32)
    enable_language(RC)
    set(REDBARON_ICON "${CMAKE_CURRENT_BINARY_DIR}/RedBaron.ico")
    execute_process(COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/make-icon.ps1" -Output "${REDBARON_ICON}"
        COMMAND_ERROR_IS_FATAL ANY)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/redbaron_brand.rc"
        "1 ICON \"${REDBARON_ICON}\"\n2 RCDATA \"${REDBARON_ICON}\"\n")
    add_executable(snoopyflyingace WIN32 ${SNOOPYFLYINGACE_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/redbaron_brand.rc")
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
set(REDBARON_EOS_SDK "" CACHE PATH "Official EOS SDK for in-game online play (optional)")
if(REDBARON_EOS_SDK)
    add_subdirectory(eos)
else()
    get_filename_component(REDBARON_EOS_BUNDLE_ROOT "${rexglue_DIR}/../../.." ABSOLUTE)
    if(EXISTS "${REDBARON_EOS_BUNDLE_ROOT}/lib/redbaron_eos_service.lib")
        include(eos/bundled.cmake)
    endif()
endif()
if(TARGET redbaron_eos_service)
    target_link_libraries(snoopyflyingace PRIVATE redbaron_eos_service)
    target_compile_definitions(snoopyflyingace PRIVATE REDBARON_EOS_ENABLED=1)
    if(REDBARON_EOS_SDK)
        set(REDBARON_EOS_DLL "${REDBARON_EOS_SDK}/Bin/EOSSDK-Win64-Shipping.dll")
    else()
        set(REDBARON_EOS_DLL "${REDBARON_EOS_BUNDLE_ROOT}/bin/EOSSDK-Win64-Shipping.dll")
    endif()
    add_custom_command(TARGET snoopyflyingace POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${REDBARON_EOS_DLL}" "$<TARGET_FILE_DIR:snoopyflyingace>")
    get_filename_component(REDBARON_SOCIAL_BIN "${REDBARON_EOS_DLL}" DIRECTORY)
    get_filename_component(REDBARON_SOCIAL_BUNDLE "${rexglue_DIR}/../../.." ABSOLUTE)
    foreach(component RedBaron_Online.exe RedBaron_Online.ini EpicOnlineServicesInstaller.exe)
        set(REDBARON_SOCIAL_COMPONENT "${REDBARON_SOCIAL_BIN}/${component}")
        if(NOT EXISTS "${REDBARON_SOCIAL_COMPONENT}")
            set(REDBARON_SOCIAL_COMPONENT "${REDBARON_SOCIAL_BUNDLE}/bin/${component}")
        endif()
        if(EXISTS "${REDBARON_SOCIAL_COMPONENT}")
            add_custom_command(TARGET snoopyflyingace POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${REDBARON_SOCIAL_COMPONENT}" "$<TARGET_FILE_DIR:snoopyflyingace>")
        endif()
    endforeach()
endif()
option(REDBARON_INPUT_FEED "Enable scripted controllers in a diagnostic build only" OFF)
if(REDBARON_INPUT_FEED)
    set_source_files_properties(src/local_input.cpp PROPERTIES COMPILE_DEFINITIONS REDBARON_INPUT_FEED=1)
endif()
option(REDBARON_BUILD_RUNTIME_TESTS "Build runtime integration tests (no game assets)" OFF)
if(REDBARON_BUILD_RUNTIME_TESTS)
    enable_testing()
    add_executable(redbaron_runtime_tests tests/runtime_services_test.cpp)
    target_link_libraries(redbaron_runtime_tests PRIVATE rex::runtime ws2_32)
    add_test(NAME redbaron_runtime_services COMMAND redbaron_runtime_tests
        "${CMAKE_CURRENT_BINARY_DIR}/runtime-test-userdata")
    set_tests_properties(redbaron_runtime_services PROPERTIES TIMEOUT 30)
endif()

if(WIN32)
    add_executable(redbaron_probe src/launcher_probe.cpp src/video.cpp)
    target_compile_definitions(redbaron_probe PRIVATE REDBARON_NO_GUEST_SPLASH=1)
    if(TARGET redbaron_eos_service)
        target_compile_definitions(redbaron_probe PRIVATE REDBARON_EOS_ENABLED=1)
    endif()
    target_link_libraries(redbaron_probe PRIVATE rex::runtime)
    add_executable(redbaron_content_inspect src/content_inspect.cpp)
    target_link_libraries(redbaron_content_inspect PRIVATE rex::runtime)
    # GPU plugins are loaded dynamically, so TARGET_RUNTIME_DLLS misses them.
    # Keep the runtime/plugin pair and optional upscalers together for both
    # hosts, including direct CMake builds (not just scripts/build.ps1).
    get_filename_component(REDBARON_SDK_ROOT "${rexglue_DIR}/../../.." ABSOLUTE)
    set(REDBARON_RUNTIME_HOSTS snoopyflyingace redbaron_probe redbaron_content_inspect)
    if(TARGET redbaron_runtime_tests)
        list(APPEND REDBARON_RUNTIME_HOSTS redbaron_runtime_tests)
    endif()
    foreach(runtime_asset rexruntime.dll rexgpu-xenos.dll amd_fidelityfx_upscaler_dx12.dll FidelityFX-LICENSE.txt nvngx_dlss.dll NVIDIA-DLSS-LICENSE.txt)
        set(runtime_source "${REDBARON_SDK_ROOT}/bin/${runtime_asset}")
        if(EXISTS "${runtime_source}")
            foreach(host IN LISTS REDBARON_RUNTIME_HOSTS)
                add_custom_command(TARGET ${host} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${runtime_source}" "$<TARGET_FILE_DIR:${host}>" VERBATIM)
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
