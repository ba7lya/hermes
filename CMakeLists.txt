# SPDX-License-Identifier: MIT
# --- cmake version ---
cmake_minimum_required(VERSION 3.21)

# --- project ---
project(ba7lya.hermes VERSION 1.0.0 LANGUAGES CXX)

# --- build options ---
option(${PROJECT_NAME}_BUILD_BENCHMARK "Build benchmarks"    OFF)
option(${PROJECT_NAME}_BUILD_EXAMPLE   "Build examples"      OFF)
option(${PROJECT_NAME}_BUILD_TEST      "Build Test"          OFF)

# --- includes ---
include(CTest)
include(GNUInstallDirs)

# --- dependencies ---
find_package(asio    CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
if(${PROJECT_NAME}_BUILD_BENCHMARK)
    find_package(benchmark CONFIG REQUIRED)
endif()
if(${PROJECT_NAME}_BUILD_TEST)
    find_package(GTest CONFIG REQUIRED)
endif()

# --- os ---
if (WIN32)
    add_definitions(-DWINVER=0x0A00)       # Windows 10
    add_definitions(-D_WIN32_WINNT=0x0A00) # Windows 10
    add_definitions(-DNOMINMAX)            # disable C2589 min()/max() error
endif()

# --- modules ---
add_subdirectory(thirdparty)

# Clang 17/18 cannot compile GCC 14's C++23 tuple constructors; use the GCC 13
# libstdc++ that the Clang setup action installs alongside those compilers.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19
   AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(HERMES_GCC13_INSTALL_DIR "/usr/lib/gcc/x86_64-linux-gnu/13")
    if(NOT EXISTS "${HERMES_GCC13_INSTALL_DIR}")
        message(FATAL_ERROR "Clang ${CMAKE_CXX_COMPILER_VERSION} needs the GCC 13 headers in ${HERMES_GCC13_INSTALL_DIR}")
    endif()
    add_compile_options("--gcc-install-dir=${HERMES_GCC13_INSTALL_DIR}")
    add_link_options("--gcc-install-dir=${HERMES_GCC13_INSTALL_DIR}")
endif()

# --- sanitizer ---
set(${PROJECT_NAME}_SANITIZER "" CACHE STRING "Sanitizer to build with: empty, address, thread, undefined")
set_property(CACHE ${PROJECT_NAME}_SANITIZER PROPERTY STRINGS "" address thread undefined)

function(hermes_enable_sanitizer TARGET)
    if(NOT ${PROJECT_NAME}_SANITIZER)
        return()
    endif()
    if(MSVC)
        if(NOT "${${PROJECT_NAME}_SANITIZER}" STREQUAL "address")
            message(FATAL_ERROR "MSVC supports only the address sanitizer, not ${${PROJECT_NAME}_SANITIZER}")
        endif()
        target_compile_options(${TARGET} PRIVATE /fsanitize=address)
        target_link_options(${TARGET} PRIVATE /fsanitize=address)
    else()
        if(NOT ${PROJECT_NAME}_SANITIZER MATCHES "^(address|thread|undefined)$")
            message(FATAL_ERROR "unsupported sanitizer: ${PROJECT_NAME}_SANITIZER")
        endif()
        target_compile_options(${TARGET} PRIVATE -fsanitize=${${PROJECT_NAME}_SANITIZER} -fno-omit-frame-pointer)
        target_link_options(${TARGET} PRIVATE -fsanitize=${${PROJECT_NAME}_SANITIZER})
    endif()
endfunction()

# --- target ---
add_library(${PROJECT_NAME})
add_library(ba7lya::hermes ALIAS ${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME hermes DEBUG_POSTFIX d)
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_sources(${PROJECT_NAME} PRIVATE
    src/configuration.cxx
    src/configuration_client.cxx
    src/configuration_service.cxx
    src/downstream.cxx
    src/framing.cxx
    src/horizontal.cxx
    src/net.cxx
    src/upstream.cxx
    src/vertical.cxx
    src/vertical_client.cxx
    src/vertical_service.cxx
    src/xml.cxx
)
target_link_libraries(${PROJECT_NAME} PUBLIC asio::asio pugixml::pugixml)
hermes_enable_sanitizer(${PROJECT_NAME})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
# the traditional msvc preprocessor cannot count a __VA_ARGS__ list, so the conformant one is
# required; it has to be set on the library too, or its headers would expand differently
target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>")

# every consumer target repeats the language level, the msvc flags and the sanitizer
function(hermes_configure_consumer TARGET)
    target_compile_features(${TARGET} PRIVATE cxx_std_23)
    target_compile_options(${TARGET} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    target_compile_options(${TARGET} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
    target_compile_options(${TARGET} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>")
    hermes_enable_sanitizer(${TARGET})
endfunction()

# --- install ---
set(HERMES_PUBLIC_HEADERS
    include/channel.hxx
    include/configuration_client.hxx
    include/configuration_service.hxx
    include/downstream.hxx
    include/enums.hxx
    include/error.hxx
    include/expected.hxx
    include/hermes.hxx
    include/messages.hxx
    include/settings.hxx
    include/types.hxx
    include/upstream.hxx
    include/version.hxx
    include/vertical_client.hxx
    include/vertical_service.hxx
    include/xml.hxx
)
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/hermes-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    cmake/hermes-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/hermes-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hermes
)
install(TARGETS ${PROJECT_NAME} EXPORT hermes-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${HERMES_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ba7lya/hermes)
install(EXPORT hermes-targets
    NAMESPACE ba7lya::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hermes
    FILE hermes-targets.cmake
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/hermes-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/hermes-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hermes
)

# --- benchmarks ---
if(${PROJECT_NAME}_BUILD_BENCHMARK)
    add_subdirectory(benchmarks)
endif()

# --- examples ---
if(${PROJECT_NAME}_BUILD_EXAMPLE)
    add_subdirectory(examples)
endif()

# --- unit tests ---
if(${PROJECT_NAME}_BUILD_TEST)
    enable_testing()
    include(GoogleTest)
    add_subdirectory(tests)
endif()
