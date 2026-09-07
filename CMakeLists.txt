cmake_minimum_required(VERSION 3.20)
project(OrbitaSystem VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release Engineering metadata. CMAKE_PROJECT_VERSION is the canonical product version.
set(MILTECH_RELEASE_CHANNEL "internal" CACHE STRING "MilTech Station release channel")
set_property(CACHE MILTECH_RELEASE_CHANNEL PROPERTY STRINGS internal pilot stable)

set(MILTECH_GIT_SHA "" CACHE STRING "Git commit SHA embedded into release metadata")
if(NOT MILTECH_GIT_SHA)
    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE MILTECH_GIT_RESULT
        OUTPUT_VARIABLE MILTECH_GIT_SHA_DETECTED
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(MILTECH_GIT_RESULT EQUAL 0 AND MILTECH_GIT_SHA_DETECTED)
        set(MILTECH_GIT_SHA "${MILTECH_GIT_SHA_DETECTED}")
    else()
        set(MILTECH_GIT_SHA "unknown")
    endif()
endif()

set(MILTECH_BUILD_UTC "" CACHE STRING "UTC build timestamp (ISO-8601); CI may set this for reproducibility")
if(NOT MILTECH_BUILD_UTC)
    string(TIMESTAMP MILTECH_BUILD_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/release/build-info.json.in"
    "${CMAKE_BINARY_DIR}/build-info.json"
    @ONLY
)

include(CTest)

add_subdirectory(orbita)
add_subdirectory(stand)
add_subdirectory(registrar)
add_subdirectory(desktop_orbita)
add_subdirectory(release)
