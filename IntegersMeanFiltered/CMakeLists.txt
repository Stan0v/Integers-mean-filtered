# include source and define project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
#include_directories($ENV{HOMEDRIVE}/dev/benchmark/include/)

find_package(benchmark REQUIRED)

# Add source to this project's executable.
add_executable (IntegersMeanFiltered "IntegersMeanFiltered.cpp" "IntegersMeanFiltered.h"   "functionwrapper.h" "threadpool.h" "threadsafequeue.h" "threadwrapper.h")
target_compile_features(IntegersMeanFiltered PRIVATE cxx_std_20)
target_link_libraries(IntegersMeanFiltered benchmark::benchmark)
target_link_libraries(IntegersMeanFiltered benchmark::benchmark_main)

# TODO: Add tests and install targets if needed.
