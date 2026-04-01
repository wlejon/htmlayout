add_executable(htmlayout_test
    main.cpp
    test_foundation.cpp
    test_tokenizer.cpp
    test_parser.cpp
    test_selector.cpp
    test_cascade.cpp
    test_shorthand.cpp
    test_layout.cpp
    test_inline.cpp
    test_flex.cpp
    test_hittest.cpp
    test_calc.cpp
    test_grid.cpp
    test_remaining.cpp
    test_final.cpp
    test_webcomponents.cpp
    test_coverage.cpp
)
target_include_directories(htmlayout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(htmlayout_test PRIVATE htmlayout gumbo)
