cmake_minimum_required(VERSION 3.10)
project(DistributedCache)

set(CMAKE_CXX_STANDARD 17)

include_directories(${PROJECT_SOURCE_DIR}/include)

# Find Boost
find_package(Boost REQUIRED)
# Include Boost headers
include_directories(${Boost_INCLUDE_DIRS})

# Add GoogleTest
# find_package(GTest REQUIRED)

# Include Conan dependencies
# include(${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake)

# Adjust this path to the actual location of your conan_toolchain.cmake
set(CMAKE_TOOLCHAIN_FILE "/Users/donalshijan/Workspace/PersonalProjects/Distributed_Cache/build/Release/generators/conan_toolchain.cmake")

file(GLOB SOURCES "src/*.cpp")

add_executable(distributed_cache ${SOURCES})

# Add Google Test for unit testing
# enable_testing()

# # Create the test executable
# add_executable(run_tests tests/test_cache.cpp src/cache.cpp src/cache_node.cpp)

# # Link Google Test and other required libraries
# target_link_libraries(run_tests ${CONAN_LIBS} GTest::gtest GTest::gtest_main)

# Set up the tests
add_test(NAME CacheTests COMMAND run_tests)