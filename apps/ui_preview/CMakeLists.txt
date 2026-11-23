cmake_minimum_required(VERSION 3.20)
project(MilTechStationUiPreview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)

add_executable(MilTechStationUiPreview
    main.cpp
    components.cpp
    components.h
    ui_composition.cpp
    ui_composition.h
    platform_pages.cpp
    platform_pages.h
    resources.qrc
)

target_link_libraries(MilTechStationUiPreview PRIVATE Qt6::Widgets)

if(WIN32)
    set_target_properties(MilTechStationUiPreview PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
