cmake_minimum_required(VERSION 3.21)

project(radeau LANGUAGES CXX)

add_executable(radeau src/radeau.cpp)
target_link_libraries(radeau PRIVATE raylib)
set_property(TARGET radeau PROPERTY CXX_STANDARD 20)
