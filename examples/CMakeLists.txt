# SPDX-License-Identifier: MIT

# every example is a standalone program on the public API, so it only links the library itself
function(hermes_add_example NAME)
    add_executable(${NAME} ${NAME}.cxx)
    target_link_libraries(${NAME} PRIVATE ba7lya::hermes)
    hermes_configure_consumer(${NAME})
endfunction()

hermes_add_example(downstream_upstream_pair)
hermes_add_example(configuration_example)
hermes_add_example(vertical_example)
