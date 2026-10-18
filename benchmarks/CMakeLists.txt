# SPDX-License-Identifier: MIT

# the source supplies its own main() so that every benchmark is registered before the library
# parses the command line, hence no benchmark::benchmark_main here
add_executable(${PROJECT_NAME}_benchmark serializer_benchmark.cxx)
hermes_configure_consumer(${PROJECT_NAME}_benchmark)
target_link_libraries(${PROJECT_NAME}_benchmark PRIVATE ba7lya::hermes benchmark::benchmark)