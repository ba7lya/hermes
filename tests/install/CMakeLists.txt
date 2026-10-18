# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.21)

# The installed package carries the configurations that were installed; default to both for a
# local smoke test, while CI passes the one configuration it installed.
if(NOT DEFINED CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
endif()
project(hermes_install_consumer LANGUAGES CXX)

# This project is built by CI against an installed prefix only; it proves the package config,
# the imported target and the nested include layout work without the source tree.
find_package(hermes CONFIG REQUIRED)

add_executable(hermes_install_consumer main.cxx)
target_link_libraries(hermes_install_consumer PRIVATE ba7lya::hermes)
