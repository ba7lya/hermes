# SPDX-License-Identifier: MIT

# one executable per test type; the socket based suites bind loopback ports, so they are not run
# in parallel with each other (RUN_SERIAL below)
function(hermes_add_tests TARGET)
    add_executable(${TARGET} ${ARGN})
    target_link_libraries(${TARGET} PRIVATE ba7lya::hermes GTest::gtest_main asio::asio pugixml::pugixml)
    hermes_configure_consumer(${TARGET})
    target_include_directories(${TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/tests ${PROJECT_SOURCE_DIR}/src)
    # Register every GoogleTest case with CTest so the editor test explorer can run it directly.
    gtest_discover_tests(${TARGET} PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
endfunction()

hermes_add_tests(${PROJECT_NAME}_ut
    ut/framing_test.cxx
    ut/state_machine_test.cxx
    ut/xml_test.cxx
)

hermes_add_tests(${PROJECT_NAME}_integration
    integration/configuration_test.cxx
    integration/horizontal_test.cxx
)

hermes_add_tests(${PROJECT_NAME}_e2e
    e2e/raw_xml_test.cxx
    e2e/vertical_test.cxx
)
