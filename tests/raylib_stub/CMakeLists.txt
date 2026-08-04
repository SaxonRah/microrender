cmake_minimum_required(VERSION 3.16)
project(raylib_stub C)
add_library(raylib STATIC raylib_stub.c)
target_include_directories(raylib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
