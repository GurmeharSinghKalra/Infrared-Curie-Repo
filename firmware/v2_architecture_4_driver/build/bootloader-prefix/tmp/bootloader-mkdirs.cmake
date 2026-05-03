# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/gurmehar/esp/esp-idf/components/bootloader/subproject"
  "/home/gurmehar/.gemini/antigravity/playground/infrared-curie/firmware/idf_robot_v2/build/bootloader"
  "/home/gurmehar/.gemini/antigravity/playground/infrared-curie/firmware/idf_robot_v2/build/bootloader-prefix"
  "/home/gurmehar/.gemini/antigravity/playground/infrared-curie/firmware/idf_robot_v2/build/bootloader-prefix/tmp"
  "/home/gurmehar/.gemini/antigravity/playground/infrared-curie/firmware/idf_robot_v2/build/bootloader-prefix/src/bootloader-stamp"
  "/home/gurmehar/.gemini/antigravity/playground/infrared-curie/firmware/idf_robot_v2/build/bootloader-prefix/src"
  "/home/gurmehar/.gemini/antigravity/playground/infrared-curie/firmware/idf_robot_v2/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/gurmehar/.gemini/antigravity/playground/infrared-curie/firmware/idf_robot_v2/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/gurmehar/.gemini/antigravity/playground/infrared-curie/firmware/idf_robot_v2/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
