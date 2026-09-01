# Joy-Con Bridge — top-level ESP-IDF project.
# Build: idf.py set-target esp32 && idf.py build
cmake_minimum_required(VERSION 3.16)

# Bluepad32 owns the shared Security Manager callback and assumes every LE
# connection belongs to its controller table. This project also hosts an LE
# Xbox HID peripheral, so preserve unknown LE connections for that profile.
set(BLUEPAD32_LE_FILE "${CMAKE_CURRENT_LIST_DIR}/managed_components/bluepad32/bt/uni_bt_le.c")
if(EXISTS "${BLUEPAD32_LE_FILE}")
    file(READ "${BLUEPAD32_LE_FILE}" BLUEPAD32_LE_SOURCE)
    set(BLUEPAD32_LE_OLD [=[            if (!device) {
                loge("SM_EVENT_PAIRING_COMPLETE: Invalid device for addr %s\n", bd_addr_to_str(addr));
                hog_disconnect(con_handle);
                break;
            }]=])
    set(BLUEPAD32_LE_NEW [=[            if (!device) {
                loge("SM_EVENT_PAIRING_COMPLETE: Invalid device for addr %s\n", bd_addr_to_str(addr));
                // Application-owned LE connection; do not disconnect it.
                break;
            }]=])
    string(REPLACE "${BLUEPAD32_LE_OLD}" "${BLUEPAD32_LE_NEW}" BLUEPAD32_LE_SOURCE
           "${BLUEPAD32_LE_SOURCE}")
    file(WRITE "${BLUEPAD32_LE_FILE}" "${BLUEPAD32_LE_SOURCE}")
endif()

# components/ is auto-discovered; managed deps (bluepad32) come from
# main/idf_component.yml via the component manager.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(joycon_bridge)
