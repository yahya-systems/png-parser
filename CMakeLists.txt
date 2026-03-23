cmake_minimum_required(VERSION 3.20)
project(png-parser CXX)

set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(png-parser STATIC src/image-processor.cpp src/deflate/deflate.cpp)

target_compile_features(png-parser PUBLIC cxx_std_20)
target_include_directories(png-parser PUBLIC include/)
