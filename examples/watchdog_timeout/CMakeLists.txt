# Watchdog Timeout Example for Carbon ESP32 SDK

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "../../components"
    "../../managed_components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(watchdog_timeout_example)
