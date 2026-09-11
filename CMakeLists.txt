cmake_minimum_required(VERSION 3.20)
project(MilTechStation VERSION 2.0.0 LANGUAGES CXX)

# yaml-cpp 0.8.0 still declares CMake 3.4. CMake 4 removed compatibility
# below 3.5, so keep the compatibility fix in the project instead of relying
# on a command-line patch remembered by each developer or build agent.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

# Legacy Orbita telemetry remains a separate subsystem. KTMA/UBSI application
# code lives under ktma/ and must not depend on the Orbita/E20 product model.
add_subdirectory(third_party/yaml-cpp)
add_subdirectory(orbita)
add_subdirectory(station)
add_subdirectory(deliveries/ktma/registrar)
add_subdirectory(deliveries/ktma/ubsi)
add_subdirectory(apps/desktop)
add_subdirectory(apps/simulator)

