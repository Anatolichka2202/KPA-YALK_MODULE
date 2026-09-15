find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

add_executable(MilTechStationSimulator WIN32 main.cpp)
target_compile_features(MilTechStationSimulator PRIVATE cxx_std_17)
target_link_libraries(MilTechStationSimulator PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
set_target_properties(MilTechStationSimulator PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps/desktop")

if(BUILD_TESTING)
    add_executable(simulator_protocol_test tests/simulator_protocol_test.cpp)
    target_compile_features(simulator_protocol_test PRIVATE cxx_std_17)
    target_link_libraries(simulator_protocol_test PRIVATE Qt6::Core Qt6::Network)
    add_dependencies(simulator_protocol_test MilTechStationSimulator)
    add_test(NAME simulator.protocol
        COMMAND simulator_protocol_test "$<TARGET_FILE:MilTechStationSimulator>")
    set_tests_properties(simulator.protocol PROPERTIES TIMEOUT 15)
endif()
