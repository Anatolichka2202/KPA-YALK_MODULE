find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

add_executable(MilTechStationSimulator WIN32 main.cpp)
target_compile_features(MilTechStationSimulator PRIVATE cxx_std_17)
target_link_libraries(MilTechStationSimulator PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
set_target_properties(MilTechStationSimulator PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps/desktop")
