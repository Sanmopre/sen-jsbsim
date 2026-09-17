cmake_minimum_required(VERSION 3.15)
project(sen-jsbsim CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(sen REQUIRED CONFIG)
find_package(jsbsim REQUIRED CONFIG)

add_executable(${PROJECT_NAME} source/main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE sen::core sen::kernel sen::db sen::util jsbsim::jsbsim)
