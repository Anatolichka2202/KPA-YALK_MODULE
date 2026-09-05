cmake_minimum_required(VERSION 3.20)
project(OrbitaSystem VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

add_subdirectory(orbita)
add_subdirectory(stand)
add_subdirectory(registrar)
add_subdirectory(desktop_orbita)
