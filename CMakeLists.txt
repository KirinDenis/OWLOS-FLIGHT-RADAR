cmake_minimum_required(VERSION 3.16)

# idf.py and the ESP-IDF extension build the folder you opened. The sources stay in
# firmware/. project() has to be in this file: the chip's toolchain is chosen while it
# runs, and an include() of firmware/CMakeLists.txt leaves the host compiler in place.
# Opening the repo and setting the target to esp32s3 builds the plain Elecrow
# CrowPanel 5.0 (DIS07050H). The Advance is -DRADAR_DISPLAY=10, or
# firmware/BUILD.BAT crowadv50. The numbers are in firmware/CMakeLists.txt.
if(NOT DEFINED RADAR_DISPLAY)
    set(RADAR_DISPLAY 15)
endif()

# Build, with no chip chosen, would pick an ESP32. the 5" is an S3. a target passed in
# (the extension's Set Target, or -DIDF_TARGET) wins over this
if(NOT DEFINED IDF_TARGET AND NOT DEFINED ENV{IDF_TARGET})
    if(RADAR_DISPLAY EQUAL 4)
        set(IDF_TARGET esp32c3)
    elseif(RADAR_DISPLAY EQUAL 8)
        set(IDF_TARGET esp32c5)
    elseif(RADAR_DISPLAY EQUAL 12)
        set(IDF_TARGET esp32p4)
    elseif(RADAR_DISPLAY EQUAL 1 OR RADAR_DISPLAY EQUAL 2 OR RADAR_DISPLAY EQUAL 3)
        set(IDF_TARGET esp32)
    else()
        set(IDF_TARGET esp32s3)
    endif()
endif()

set(_fw "${CMAKE_CURRENT_LIST_DIR}/firmware")
set(EXTRA_COMPONENT_DIRS "${_fw}/main" "${CMAKE_CURRENT_LIST_DIR}/core")

# the partition csv names in the defaults are relative to the project directory, which
# here is the repo root. the files themselves stay in firmware/, so the copies the
# build reads name them from there. the chip file (sdkconfig.defaults.<chip>) is picked
# up beside the first one, the same way a build from firmware/ does it
set(_gen "${CMAKE_BINARY_DIR}/owlos-defaults")
file(MAKE_DIRECTORY "${_gen}")

function(_owlos_defaults src dst)
    file(READ "${src}" _body)
    string(REPLACE "\"partitions.csv\"" "\"${_fw}/partitions.csv\"" _body "${_body}")
    string(REPLACE "\"partitions_2mb.csv\"" "\"${_fw}/partitions_2mb.csv\"" _body "${_body}")
    string(REPLACE "\"partitions_16mb.csv\"" "\"${_fw}/partitions_16mb.csv\"" _body "${_body}")
    file(WRITE "${dst}" "${_body}")
endfunction()

file(GLOB _src_defaults "${_fw}/sdkconfig.defaults*")
foreach(_src ${_src_defaults})
    get_filename_component(_name "${_src}" NAME)
    _owlos_defaults("${_src}" "${_gen}/${_name}")
endforeach()

if(RADAR_DISPLAY EQUAL 7 OR RADAR_DISPLAY EQUAL 9)   # the S3 boards with an LCD
    set(SDKCONFIG_DEFAULTS "${_gen}/sdkconfig.defaults;${_gen}/sdkconfig.defaults.s3lcd")
elseif(RADAR_DISPLAY EQUAL 10 OR RADAR_DISPLAY EQUAL 11 OR RADAR_DISPLAY EQUAL 13 OR RADAR_DISPLAY EQUAL 14 OR RADAR_DISPLAY EQUAL 15)
    set(SDKCONFIG_DEFAULTS "${_gen}/sdkconfig.defaults;${_gen}/sdkconfig.defaults.s3lcd;${_gen}/sdkconfig.defaults.s3rgb")
else()
    set(SDKCONFIG_DEFAULTS "${_gen}/sdkconfig.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(COMPILE_DEFINITIONS "RADAR_DISPLAY=${RADAR_DISPLAY}" APPEND)

set(CHANGES "${CMAKE_CURRENT_LIST_DIR}/CHANGES.md")
file(STRINGS "${CHANGES}" FLIGHT_VERSION REGEX "^## [0-9]" LIMIT_COUNT 1)
string(REGEX REPLACE "^## ([0-9.]+).*" "\\1" FLIGHT_VERSION "${FLIGHT_VERSION}")
if(NOT FLIGHT_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
    message(FATAL_ERROR "no version in ${CHANGES}: its first \"## \" line should be one, like ## 0.3")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CHANGES}")
idf_build_set_property(COMPILE_DEFINITIONS "FLIGHT_VERSION=\"${FLIGHT_VERSION}\"" APPEND)
project(owlos-flight-radar)
