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

# The approved TU drawing/model is imported under ui/ and model/ verbatim first.
# It is deliberately not linked to the clean executable until its old type names
# are mechanically detached. No legacy Orbita target is pulled as a shortcut.

if(BUILD_TESTING)
    add_executable(scenario_engine_test tests/scenario_engine_test.cpp)
    target_link_libraries(scenario_engine_test PRIVATE tu_backend)
    target_compile_definitions(scenario_engine_test PRIVATE
        TU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME tu.scenario_engine COMMAND scenario_engine_test)
endif()
