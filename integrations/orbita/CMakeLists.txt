add_library(miltech_orbita_integration STATIC
    src/orbita_sample_bridge.cpp
)

target_include_directories(miltech_orbita_integration
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(miltech_orbita_integration PUBLIC cxx_std_17)
target_link_libraries(miltech_orbita_integration
    PUBLIC
        orbita
        orbita_stand_runtime
)

add_library(miltech::orbita_integration ALIAS miltech_orbita_integration)

if(BUILD_TESTING)
    add_executable(orbita_sample_bridge_test
        tests/orbita_sample_bridge_test.cpp
    )
    target_link_libraries(orbita_sample_bridge_test PRIVATE
        miltech_orbita_integration
        orbita
        orbita_stand_runtime
    )
    add_test(NAME integration.orbita_sample_bridge COMMAND orbita_sample_bridge_test)
endif()
