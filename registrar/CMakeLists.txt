cmake_minimum_required(VERSION 3.20)

project(registrar VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Sql)

add_executable(registrar
    include/model.h
    include/registrar.h
    src/registrar.cpp
    src/registrar_cli.cpp
)

target_include_directories(registrar
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(registrar
    PRIVATE
        Qt6::Core
        Qt6::Sql
)
