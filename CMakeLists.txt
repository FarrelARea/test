cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{GTM_PATH})
    message(FATAL_ERROR "Set GTM_PATH to a clone of https://github.com/wireless-tag-com/GTM")
endif()

set(GTM_EXAMPLE "$ENV{GTM_PATH}/doc/Schematic/examples/Esp32AgentDevKit")
set(ENV{QMSD_8MS_PATH} "${GTM_EXAMPLE}/qmsd-esp32-bsp")
list(APPEND EXTRA_COMPONENT_DIRS "${GTM_EXAMPLE}/components/QM-Y1091-4832")

include($ENV{QMSD_8MS_PATH}/component.cmake)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(virtualkeyboard)
