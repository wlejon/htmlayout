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
    test_spec_completeness.cpp
    test_spec_validator.cpp
    test_transform.cpp
    test_coverage_extra.cpp
    test_layout_extra.cpp
    test_flex_grid_extra.cpp
    test_more_coverage.cpp
    test_parser_extra.cpp
    test_incremental.cpp
    test_bidi.cpp
)
target_include_directories(htmlayout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(htmlayout_test PRIVATE htmlayout gumbo)

# One aggregate binary runs every suite from main() and returns non-zero on any
# failure, so a single ctest entry is the whole surface.
add_test(NAME htmlayout_test COMMAND htmlayout_test)
