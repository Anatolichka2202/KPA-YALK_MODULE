add_library(orbita_stand_domain STATIC
    src/yalk_analog_procedure.cpp
)

target_include_directories(orbita_stand_domain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(orbita_stand_domain PUBLIC cxx_std_17)

find_package(Qt6 REQUIRED COMPONENTS Core Network SerialPort)

add_library(orbita_stand_adapters STATIC
    adapters/equipment_adapters.cpp
    adapters/v7_visa_voltmeter.cpp
    adapters/visa_instrument.cpp
)
target_include_directories(orbita_stand_adapters
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(orbita_stand_adapters PUBLIC cxx_std_17)
target_link_libraries(orbita_stand_adapters
    PUBLIC orbita_stand_domain
    PRIVATE Qt6::Core Qt6::Network Qt6::SerialPort
)

if(BUILD_TESTING)
    add_executable(yalk_analog_procedure_test
        tests/yalk_analog_procedure_test.cpp
    )
    target_link_libraries(yalk_analog_procedure_test PRIVATE orbita_stand_domain)
    add_test(NAME stand.yalk_analog_procedure COMMAND yalk_analog_procedure_test)

    add_executable(equipment_protocol_test
        tests/equipment_protocol_test.cpp
    )
    target_link_libraries(equipment_protocol_test PRIVATE orbita_stand_adapters)
    add_test(NAME stand.equipment_protocol COMMAND equipment_protocol_test)
endif()

if(WIN32)
    add_executable(v7_probe tools/v7_probe.cpp)
    target_link_libraries(v7_probe PRIVATE orbita_stand_adapters)
    if(MINGW)
        target_link_options(v7_probe PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()
