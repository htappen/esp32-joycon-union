# Joy-Con Bridge — top-level ESP-IDF project.
# Build: idf.py set-target esp32 && idf.py build
cmake_minimum_required(VERSION 3.16)

# components/ is auto-discovered; managed deps (bluepad32) come from
# main/idf_component.yml via the component manager.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(joycon_bridge)
