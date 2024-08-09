cmake_minimum_required(VERSION 3.10)
project(DistributedCache)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

# Include Conan dependencies
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

file(GLOB SOURCES "src/*.cpp")

add_executable(distributed_cache ${SOURCES})
