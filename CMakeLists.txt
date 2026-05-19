cmake_minimum_required(VERSION 3.22)

# Change name of source directory from 'main' to 'src'
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/src")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32_idf_native)

set(FLASH_IN_PROJECT "yes")

# Create filesystem image and flash it during programming
littlefs_create_partition_image(spiffs data FLASH_IN_PROJECT)