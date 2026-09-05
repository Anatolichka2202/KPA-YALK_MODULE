cmake_minimum_required(VERSION 3.20)

project(registrar VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Sql)

add_library(ktma_registrar STATIC
    include/model.h
    include/registrar.h
    include/report.h
    src/registrar.cpp
    src/report.cpp
)

target_include_directories(ktma_registrar
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ktma_registrar
    PUBLIC
        Qt6::Core
        Qt6::Sql
)

add_executable(registrar
    src/registrar_cli.cpp
)
target_link_libraries(registrar PRIVATE ktma_registrar)

if(BUILD_TESTING)
    add_executable(registrar_lifecycle_test
        tests/registrar_lifecycle_test.cpp
    )
    target_link_libraries(registrar_lifecycle_test PRIVATE ktma_registrar Qt6::Core Qt6::Sql)
    add_test(NAME registrar.lifecycle COMMAND registrar_lifecycle_test)
    if(WIN32)
        set_tests_properties(registrar.lifecycle PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()
