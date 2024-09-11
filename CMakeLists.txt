cmake_minimum_required(VERSION 3.10)
project(DistributedCache)

set(CMAKE_CXX_STANDARD 17)

include_directories(${PROJECT_SOURCE_DIR}/include)

# Find Boost
find_package(Boost REQUIRED)

# Include Boost headers
include_directories(${Boost_INCLUDE_DIRS})

# Include Conan dependencies
# include(${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake)

# Adjust this path to the actual location of your conan_toolchain.cmake
set(CMAKE_TOOLCHAIN_FILE "/Users/donalshijan/Workspace/PersonalProjects/Distributed_Cache/build/Release/generators/conan_toolchain.cmake")

file(GLOB SOURCES "src/*.cpp")

add_executable(distributed_cache ${SOURCES})
