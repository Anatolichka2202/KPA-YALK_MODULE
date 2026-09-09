cmake_minimum_required(VERSION 3.20)
project(MilTechStation VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

# Legacy Orbita telemetry remains a separate subsystem. KTMA/UBSI application
# code lives under ktma/ and must not depend on the Orbita/E20 product model.
add_subdirectory(orbita)
add_subdirectory(station)
add_subdirectory(deliveries/ktma/registrar)
add_subdirectory(deliveries/ktma/ubsi)
add_subdirectory(apps/desktop)
