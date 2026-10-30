# Authored EOS glue can be bundled as libraries, without shipping SDK headers
# or requesting a developer toolchain/account from each player.
foreach(component redbaron_eos_service redbaron_eos_wire redbaron_eos_lobby)
    if(NOT EXISTS "${REDBARON_EOS_BUNDLE_ROOT}/lib/${component}.lib")
        message(FATAL_ERROR "Incomplete bundled online provider: ${component}")
    endif()
    add_library(${component} STATIC IMPORTED)
    set_target_properties(${component} PROPERTIES IMPORTED_LOCATION "${REDBARON_EOS_BUNDLE_ROOT}/lib/${component}.lib")
endforeach()
foreach(component lib/EOSSDK-Win64-Shipping.lib bin/EOSSDK-Win64-Shipping.dll)
    if(NOT EXISTS "${REDBARON_EOS_BUNDLE_ROOT}/${component}")
        message(FATAL_ERROR "Incomplete bundled EOS runtime: ${component}")
    endif()
endforeach()
set_property(TARGET redbaron_eos_service PROPERTY INTERFACE_LINK_LIBRARIES
    "rex::runtime;redbaron_eos_wire;redbaron_eos_lobby;${REDBARON_EOS_BUNDLE_ROOT}/lib/EOSSDK-Win64-Shipping.lib;ws2_32")
