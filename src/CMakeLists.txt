add_subdirectory(css)
add_subdirectory(layout)

# Main library: combines css + layout into a single target for consumers
add_library(htmlayout INTERFACE)
target_link_libraries(htmlayout INTERFACE htmlayout_css htmlayout_layout)
target_include_directories(htmlayout INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
