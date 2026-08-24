add_library(orbita_stand_domain STATIC
    adapters/v7_visa_voltmeter.cpp
    src/yalk_analog_procedure.cpp
)

target_include_directories(orbita_stand_domain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(orbita_stand_domain PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(yalk_analog_procedure_test
        tests/yalk_analog_procedure_test.cpp
    )
    target_link_libraries(yalk_analog_procedure_test PRIVATE orbita_stand_domain)
    add_test(NAME stand.yalk_analog_procedure COMMAND yalk_analog_procedure_test)
endif()

if(WIN32)
    add_executable(v7_probe tools/v7_probe.cpp)
    target_link_libraries(v7_probe PRIVATE orbita_stand_domain)
    if(MINGW)
        target_link_options(v7_probe PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()
