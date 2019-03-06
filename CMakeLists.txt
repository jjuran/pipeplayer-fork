cmake_minimum_required(VERSION 3.1)
project(pipeplayer)

set(CMAKE_CXX_STANDARD 11)

add_subdirectory(portaudio)

add_executable(pipeplayer pipeplayer.cpp)
target_link_libraries(pipeplayer portaudio_static)
