cmake_minimum_required(VERSION 3.5)

set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(speech_recognize)
# spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)
