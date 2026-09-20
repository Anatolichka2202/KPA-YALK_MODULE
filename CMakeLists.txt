cmake_minimum_required(VERSION 3.20)
project(UbsiTuMinimal VERSION 0.1.0 LANGUAGES CXX)

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

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort Network)

# Direct clean hardware layer: no Orbita runtime/plugin ABI. VISA is resolved
# dynamically from visa64.dll/visa32.dll at runtime, so no vendor .lib is needed.
add_library(tu_hardware STATIC
    hardware/stand_config.cpp
    hardware/stand_config.h
    hardware/akip1160.cpp
    hardware/akip1160.h
    hardware/yalk_reference_link.cpp
    hardware/yalk_reference_link.h
    hardware/isd_router.cpp
    hardware/isd_router.h
    hardware/visa_instrument.cpp
    hardware/visa_instrument.h
    hardware/bench_instruments.cpp
    hardware/bench_instruments.h
    hardware/stand_hardware.cpp
    hardware/stand_hardware.h
)
target_include_directories(tu_hardware PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tu_hardware PUBLIC
    Qt6::SerialPort
    Qt6::Network
    yaml-cpp
)
if(WIN32)
    target_link_libraries(tu_hardware PUBLIC ws2_32)
endif()

add_library(tu_procedures STATIC
    procedures/power_procedures.cpp
    procedures/power_procedures.h
    procedures/yalk_procedures.cpp
    procedures/yalk_procedures.h
    procedures/yalk_verified_procedures.cpp
    procedures/yalk_verified_procedures.h
    procedures/ytp_procedures.cpp
    procedures/ytp_procedures.h
    procedures/yvp_procedures.cpp
    procedures/yvp_procedures.h
)
target_include_directories(tu_procedures PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tu_procedures PUBLIC
    tu_backend
    tu_hardware
)

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

add_executable(tu_ui_smoke tests/ui_smoke.cpp)
target_link_libraries(tu_ui_smoke PRIVATE tu_ui)

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

# Commissioning-only diagnostic. It never uses type=3, type=4/type=7 or ±12 V:
# only 3.1 V through the already confirmed type=5 YALK stimulus path. Its job is
# to prove the physical ISD channel -> logical reference204 address permutation
# before the acceptance route is allowed to rely on that mapping.
add_executable(yalk_map_probe tools/yalk_map_probe.cpp)
target_include_directories(yalk_map_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yalk_map_probe PRIVATE tu_hardware Qt6::Core)

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
