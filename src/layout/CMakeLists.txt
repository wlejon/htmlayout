add_library(htmlayout_layout STATIC
    box.cpp
    block.cpp
    inline.cpp
    flex.cpp
    text.cpp
    formatting_context.cpp
)
target_include_directories(htmlayout_layout PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(htmlayout_layout PUBLIC htmlayout_css)
