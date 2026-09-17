cmake_minimum_required(VERSION 3.15)
project(sen-jsbsim CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(sen REQUIRED CONFIG)
find_package(jsbsim REQUIRED CONFIG)
find_package(SDL3 REQUIRED CONFIG)
find_package(ftxui REQUIRED CONFIG)

add_executable(${PROJECT_NAME} 
  source/component.cpp
  source/component.h
  source/aircraft_manager.h
  source/aircraft_manager.cpp
  source/main.cpp)


sen_generate_cpp(
        TARGET
        ${PROJECT_NAME}
        HLA_FOM_DIRS
        "$ENV{STD_FOM_PATH}/rpr"
        "$ENV{STD_FOM_PATH}/mapping"
        HLA_MAPPINGS_FILE
        "$ENV{STD_FOM_PATH}/mapping/mapping.xml"
)

target_link_libraries(${PROJECT_NAME} PRIVATE sen::core sen::kernel sen::db sen::util jsbsim::jsbsim sdl::sdl ftxui::ftxui)
