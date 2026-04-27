# ESP32 HiSLIP Instrument Server for Carbon Platform
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(carbon-hislip-instrument)
