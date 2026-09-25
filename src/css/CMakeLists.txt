add_library(htmlayout_css STATIC
    tokenizer.cpp
    parser.cpp
    media_query.cpp
    selector.cpp
    cascade.cpp
    container_query.cpp
    properties.cpp
    ua_stylesheet.cpp
    color.cpp
    color_calc.cpp
    color_space.cpp
    nesting.cpp
    transform.cpp
)
target_include_directories(htmlayout_css PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
