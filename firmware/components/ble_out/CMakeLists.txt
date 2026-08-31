idf_component_register(
    SRCS "ble_xbox_hid.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES bluepad32 btstack esp_timer
)

# Generate xbox_hid.h from xbox_hid.gatt using BTstack's compiler.
# BTSTACK_ROOT is provided by the btstack managed component; adjust if the
# pinned component lays out its tools differently.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(_gatt_in  "${CMAKE_CURRENT_SOURCE_DIR}/xbox_hid.gatt")
set(_gatt_out "${CMAKE_CURRENT_BINARY_DIR}/xbox_hid.h")
set(_compile_gatt "$ENV{IDF_PATH}/components/btstack/tool/compile_gatt.py")

if(NOT EXISTS "${_compile_gatt}")
    # Fall back to the managed-component copy.
    file(GLOB_RECURSE _cg "${CMAKE_BINARY_DIR}/../managed_components/*/compile_gatt.py")
    if(NOT _cg AND DEFINED ENV{IDF_PATH})
        file(GLOB_RECURSE _cg "$ENV{IDF_PATH}/components/btstack/tool/compile_gatt.py")
    endif()
    if(_cg)
        list(GET _cg 0 _compile_gatt)
    endif()
endif()

add_custom_command(
    OUTPUT "${_gatt_out}"
    COMMAND ${Python3_EXECUTABLE} "${_compile_gatt}" "${_gatt_in}" "${_gatt_out}"
    DEPENDS "${_gatt_in}"
    COMMENT "compile_gatt: xbox_hid.gatt -> xbox_hid.h"
    VERBATIM
)
add_custom_target(xbox_hid_gatt DEPENDS "${_gatt_out}")
add_dependencies(${COMPONENT_LIB} xbox_hid_gatt)
