cmake_minimum_required(VERSION 3.16)
project(enwik9_datasheet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(enwik9_datasheet
    src/main.cpp
    src/module1_sequence.cpp
    src/module3_chunk.cpp
    src/binary_writer.cpp
)

target_include_directories(enwik9_datasheet PRIVATE src)

if (MSVC)
    target_compile_options(enwik9_datasheet PRIVATE /W4)
else()
    target_compile_options(enwik9_datasheet PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()
