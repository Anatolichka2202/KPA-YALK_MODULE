cmake_minimum_required(VERSION 3.20)
project(UbsiTuMinimal VERSION 0.1.0 LANGUAGES CXX)

# Pinned yaml-cpp still declares an old CMake baseline; CMake 4 requires this
# compatibility floor. Keep it here so the build needs no local workaround.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(CTest)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/yaml-cpp/CMakeLists.txt")
    message(FATAL_ERROR
        "yaml-cpp submodule is missing. Run: git submodule update --init --recursive")
endif()

set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/yaml-cpp EXCLUDE_FROM_ALL)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(yaml-cpp PRIVATE -include cstdint)
endif()

add_library(tu_backend STATIC
    backend/scenario_engine.cpp
    backend/scenario_yaml.cpp
)
target_include_directories(tu_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tu_backend PUBLIC yaml-cpp)

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort)

# Clean stand I/O. Only the pieces already verified on the KTMA stand are
# present here: AKIP-1160/6 USB/COM control and the confirmed ROKT YALK
# reference204 readiness sequence. No legacy Orbita runtime/plugin ABI is linked.
add_library(tu_hardware STATIC
    hardware/stand_config.cpp
    hardware/stand_config.h
    hardware/akip1160.cpp
    hardware/akip1160.h
    hardware/yalk_reference_link.cpp
    hardware/yalk_reference_link.h
    hardware/stand_hardware.cpp
    hardware/stand_hardware.h
)
target_include_directories(tu_hardware PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tu_hardware PUBLIC
    Qt6::SerialPort
    yaml-cpp
)
if(WIN32)
    target_link_libraries(tu_hardware PUBLIC ws2_32)
endif()

add_library(tu_procedures STATIC
    procedures/power_procedures.cpp
    procedures/power_procedures.h
)
target_include_directories(tu_procedures PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tu_procedures PUBLIC
    tu_backend
    tu_hardware
)

# Keep the approved/current drawing code as its own layer. The UI consumes only
# clean tu::* run types; no Orbita target or compatibility layer is linked.
add_library(tu_ui STATIC
    ui/test_page.cpp
    ui/test_page.h
    ui/tu_flow_widget.cpp
    ui/tu_flow_widget.h
    ui/ubsi_measurement_views.h
    ui/ubsi_view_controls.cpp
    model/ubsi_ui_model.h
)
target_include_directories(tu_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/model
)
target_link_libraries(tu_ui PUBLIC
    Qt6::Widgets
    tu_backend
)

# Link-only smoke executable: built by the normal build so stale UI/backend
# boundaries are caught immediately. It is intentionally not a CTest test,
# because CI/headless machines do not need to launch a Qt platform plugin.
add_executable(tu_ui_smoke tests/ui_smoke.cpp)
target_link_libraries(tu_ui_smoke PRIVATE tu_ui)

# Minimal desktop shell plus the first real hardware vertical slice.
add_executable(tu_app
    app/main.cpp
    app/tu_controller.cpp
    app/tu_controller.h
)
target_include_directories(tu_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tu_app PRIVATE
    tu_ui
    tu_procedures
)
target_compile_definitions(tu_app PRIVATE
    TU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_custom_command(TARGET tu_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:tu_app>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data/ubsi_tu.yaml"
            "$<TARGET_FILE_DIR:tu_app>/data/ubsi_tu.yaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data/stand_ktma.yaml"
            "$<TARGET_FILE_DIR:tu_app>/data/stand_ktma.yaml"
    VERBATIM
)

if(BUILD_TESTING)
    add_executable(scenario_engine_test tests/scenario_engine_test.cpp)
    target_link_libraries(scenario_engine_test PRIVATE tu_backend)
    target_compile_definitions(scenario_engine_test PRIVATE
        TU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME tu.scenario_engine COMMAND scenario_engine_test)
endif()
