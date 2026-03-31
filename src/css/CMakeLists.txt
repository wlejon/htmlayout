add_library(htmlayout_css STATIC
    tokenizer.cpp
    parser.cpp
    selector.cpp
    cascade.cpp
    properties.cpp
    ua_stylesheet.cpp
    color.cpp
)
target_include_directories(htmlayout_css PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
