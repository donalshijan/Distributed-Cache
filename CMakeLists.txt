cmake_minimum_required(VERSION 3.10)
project(DistributedCache)

set(CMAKE_CXX_STANDARD 17)

include_directories(${PROJECT_SOURCE_DIR}/include)

# Find Boost
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

# Include GTest using FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
)
FetchContent_MakeAvailable(googletest)

file(GLOB SOURCES "src/*.cpp")

option(ENABLE_PRODUCTION "Enable production mode" ON)

# Add main executable
add_executable(distributed_cache ${SOURCES})

# Link pthread
target_link_libraries(distributed_cache pthread)

# Apply production flag if enabled
if(ENABLE_PRODUCTION)
    add_definitions(-DPRODUCTION)
endif()

# Create library for cache implementation
add_library(CacheLib src/cache.cpp src/cache_node.cpp src/connection_pool.cpp)

# Test sources
file(GLOB TEST_SOURCES "tests/*.cpp")

# Add test executable
add_executable(CacheUnitTests ${TEST_SOURCES})

# Link libraries
target_link_libraries(CacheUnitTests PRIVATE CacheLib gtest_main)

# Optionally, add a custom target to run tests
add_custom_target(run_tests
    COMMAND CacheUnitTests
    DEPENDS CacheUnitTests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Enable testing
enable_testing()

# Add tests to CTest
add_test(NAME CacheTests COMMAND CacheUnitTests)
