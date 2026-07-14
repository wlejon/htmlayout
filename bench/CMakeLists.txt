# Always build and run this in a Release configuration. Timing a Debug build of
# a container-heavy C++ library measures the debug iterators, not the engine —
# and optimizing this target alone would not help, since the cost being measured
# lives inside htmlayout_css / htmlayout_layout, not in bench.cpp.
#
#   cmake -B build-release -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-release --config Release --target htmlayout_bench
add_executable(htmlayout_bench bench.cpp)
target_link_libraries(htmlayout_bench PRIVATE htmlayout)
