cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/src")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# get_filename_component(configName "${CMAKE_BINARY_DIR}" NAME)
# list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/.pio/libdeps/${configName}/esp_littlefs")


# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
# idf_build_set_property(MINIMAL_BUILD ON)

project(esp32_idf_native)
