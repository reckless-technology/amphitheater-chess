# The host's version, in the one translation unit that reads the header the configure writes, which the command line and
# the window both link.
add_library(amphitheater_version STATIC Version.cpp)
target_include_directories(amphitheater_version PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
                                            PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(amphitheater_version PRIVATE amphitheater_warnings)
