find_package(Qt6 REQUIRED COMPONENTS Core Sql)

add_library(ktma_ubsi STATIC
    include/ktma/ubsi/production.h
    include/ktma/ubsi/production_ledger.h
    src/production.cpp
    src/production_ledger.cpp
)

target_include_directories(ktma_ubsi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(ktma_ubsi PUBLIC cxx_std_17)
target_link_libraries(ktma_ubsi PUBLIC ktma_registrar Qt6::Core Qt6::Sql)

# Reporting is a separate adapter because it consumes ScenarioEngine result
# types. The UBSI application core above remains independent from Orbita/E20.
add_library(ktma_ubsi_reporting STATIC
    include/ktma/ubsi/production_report.h
    src/production_report.cpp
)
target_include_directories(ktma_ubsi_reporting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ktma_ubsi_reporting PUBLIC cxx_std_17)
target_link_libraries(ktma_ubsi_reporting PUBLIC ktma_ubsi orbita_stand_core Qt6::Core)

if(BUILD_TESTING)
    add_executable(ktma_ubsi_production_test
        tests/production_test.cpp
    )
    target_compile_definitions(ktma_ubsi_production_test PRIVATE
        KTMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(ktma_ubsi_production_test PRIVATE
        ktma_ubsi orbita_stand_reporting)
    add_test(NAME ktma.ubsi.production COMMAND ktma_ubsi_production_test)

    add_executable(ktma_ubsi_production_scenario_test
        tests/production_scenario_test.cpp
    )
    target_compile_definitions(ktma_ubsi_production_scenario_test PRIVATE
        KTMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(ktma_ubsi_production_scenario_test PRIVATE
        ktma_ubsi orbita_stand_runtime)
    add_test(NAME ktma.ubsi.production_scenarios COMMAND ktma_ubsi_production_scenario_test)
endif()

if(BUILD_TESTING)
    add_executable(ktma_ubsi_production_report_test tests/production_report_test.cpp)
    target_link_libraries(ktma_ubsi_production_report_test PRIVATE ktma_ubsi_reporting)
    add_test(NAME ktma.ubsi.production_report COMMAND ktma_ubsi_production_report_test)
endif()
if(WIN32 AND BUILD_TESTING)
    get_filename_component(_ktma_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)

    set_tests_properties(
        ktma.ubsi.production
        ktma.ubsi.production_scenarios
        ktma.ubsi.production_report
        PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_ktma_mingw_bin}"
    )
endif()
