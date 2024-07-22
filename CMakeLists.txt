cmake_minimum_required(VERSION 3.25)
project(async_http)

set(CMAKE_CXX_STANDARD 17)

link_libraries(ws2_32 wsock32)

add_executable(async_http main.cpp future.h utils.h reader.h http.h json.h buffer.h writer.h)
