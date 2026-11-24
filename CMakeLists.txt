cmake_minimum_required(VERSION 3.20)
project(BrpTuMinimal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets SerialPort Network Sql Concurrent)
qt_standard_project_setup()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/yaml-cpp/CMakeLists.txt")
    message(FATAL_ERROR "Выполните git submodule update --init для yaml-cpp")
endif()
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/yaml-cpp EXCLUDE_FROM_ALL)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
    target_compile_options(yaml-cpp PRIVATE -include cstdint)
endif()

add_subdirectory(orbita)

qt_add_library(tu_hardware STATIC
    hardware/akip1160.cpp
    hardware/isd_router.cpp
    hardware/e2010_sample_source.cpp
    hardware/visa_instrument.cpp
    hardware/v7_meter.cpp
)
target_include_directories(tu_hardware
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/hardware/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lusbapi"
)
target_link_libraries(tu_hardware PRIVATE Qt6::SerialPort Qt6::Network)

qt_add_library(brp_frames STATIC
    procedures/frame_checks.cpp
    integrations/orbita/src/orbita_sample_bridge.cpp
    integrations/orbita/src/orbita_sample_simulator.cpp
)
target_include_directories(brp_frames
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/procedures"
           "${CMAKE_CURRENT_SOURCE_DIR}/integrations/orbita/include"
           "${CMAKE_CURRENT_SOURCE_DIR}/hardware/include"
)
target_link_libraries(brp_frames PUBLIC orbita PRIVATE yaml-cpp)

qt_add_library(brp_ui STATIC
    ui/brp_tu_window.cpp
    ui/metadata_service.cpp
    ui/metadata_service.h
    ui/encoding_utils.cpp
)
target_include_directories(brp_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ui")
target_link_libraries(brp_ui
    PUBLIC Qt6::Widgets
    PRIVATE brp_frames tu_hardware yaml-cpp Qt6::SerialPort Qt6::Sql Qt6::Concurrent
)

qt_add_executable(tu_app WIN32 app/main.cpp)
target_link_libraries(tu_app PRIVATE brp_ui)

# Данные проверки и общая SQLite-БД копируются рядом с исполняемым файлом.
# Изменения YAML попадают в сборку и при отсутствии перекомпиляции.
add_custom_target(brp_runtime_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:tu_app>/data"
    DEPENDS tu_app
    VERBATIM
)
if(WIN32)
    add_custom_command(TARGET brp_runtime_data POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lusbapi/Lusbapi64.dll"
            "$<TARGET_FILE_DIR:tu_app>"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    qt_add_executable(brp_frame_checks_test tests/frame_checks_test.cpp)
    target_link_libraries(brp_frame_checks_test PRIVATE brp_frames)
    add_test(NAME brp.frame_checks COMMAND brp_frame_checks_test
        "${CMAKE_CURRENT_SOURCE_DIR}/data/frame_checks.yaml")
    set_tests_properties(brp.frame_checks PROPERTIES TIMEOUT 15)

    qt_add_executable(orbita_m8_test tests/orbita_m8_test.cpp)
    target_link_libraries(orbita_m8_test PRIVATE brp_frames Qt6::Core)
    add_test(NAME orbita.m8 COMMAND orbita_m8_test)
    set_tests_properties(orbita.m8 PROPERTIES TIMEOUT 15)

    qt_add_executable(brp_scenario_test tests/brp_scenario_test.cpp)
    target_link_libraries(brp_scenario_test PRIVATE
        brp_ui tu_hardware Qt6::Network Qt6::Concurrent)
    add_dependencies(brp_scenario_test brp_runtime_data)
    add_test(NAME brp.scenario COMMAND brp_scenario_test -platform offscreen)
    set_tests_properties(brp.scenario PROPERTIES TIMEOUT 30)
    if(WIN32)
        set_tests_properties(brp.frame_checks brp.scenario orbita.m8 PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()
