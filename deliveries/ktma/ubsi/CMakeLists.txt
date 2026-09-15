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

# KTMA/UBSI procedure implementation belongs to the delivery/object package,
# not to the universal Station runtime. The compatibility procedure headers
# live in this delivery include tree, even while their historic include paths
# are preserved for callers during API migration.
add_library(ktma_ubsi_procedures STATIC
    src/procedures/yalk_analog_procedure.cpp
    src/procedures/ubsi_procedures.cpp
    src/procedures/ubsi_procedures_current.cpp
    src/procedures/ubsi_procedures_rokt.cpp
    src/procedures/ubsi_procedures_v7.cpp
)

# Layer the historical implementation, current KTMA overrides and ROKT
# commissioning helpers first. ubsi_procedures_v7.cpp owns the final public
# registerUbsiProcedures() symbol and installs the production V7+ISD YVP alias
# last, so `ubsi.yvp` cannot silently fall back to the adapter/ROKT path.
set_source_files_properties(src/procedures/ubsi_procedures.cpp PROPERTIES
    COMPILE_DEFINITIONS "registerUbsiProcedures=registerLegacyUbsiProcedures")
set_source_files_properties(src/procedures/ubsi_procedures_current.cpp PROPERTIES
    COMPILE_DEFINITIONS "registerUbsiProcedures=registerCurrentUbsiProcedures")
set_source_files_properties(src/procedures/ubsi_procedures_rokt.cpp PROPERTIES
    COMPILE_DEFINITIONS "registerUbsiProcedures=registerRoktUbsiProcedures")

target_include_directories(ktma_ubsi_procedures
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(ktma_ubsi_procedures PUBLIC cxx_std_17)
target_link_libraries(ktma_ubsi_procedures PUBLIC orbita_stand_core)
add_library(ktma::ubsi_procedures ALIAS ktma_ubsi_procedures)

# Physical readiness order belongs to this delivery because it encodes the
# UBSI power dependency: arm DUT supply, probe independent equipment, wait for
# the powered adapter to boot, then probe the adapter. The implementation is
# deliberately UI-free and consumes only the universal component profile.
add_library(ktma_ubsi_readiness STATIC
    src/equipment_readiness.cpp
)
target_include_directories(ktma_ubsi_readiness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ktma_ubsi_readiness PUBLIC cxx_std_17)
target_link_libraries(ktma_ubsi_readiness PUBLIC orbita_stand_runtime)
add_library(ktma::ubsi_readiness ALIAS ktma_ubsi_readiness)

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
    add_executable(ktma_ubsi_production_test tests/production_test.cpp)
    target_compile_definitions(ktma_ubsi_production_test PRIVATE
        KTMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(ktma_ubsi_production_test PRIVATE
        ktma_ubsi orbita_stand_reporting)
    add_test(NAME ktma.ubsi.production COMMAND ktma_ubsi_production_test)

    add_executable(ktma_ubsi_production_scenario_test tests/production_scenario_test.cpp)
    target_compile_definitions(ktma_ubsi_production_scenario_test PRIVATE
        KTMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(ktma_ubsi_production_scenario_test PRIVATE
        ktma_ubsi orbita_stand_runtime ktma_ubsi_procedures)
    add_test(NAME ktma.ubsi.production_scenarios COMMAND ktma_ubsi_production_scenario_test)

    add_executable(ktma_ubsi_equipment_readiness_test tests/equipment_readiness_test.cpp)
    target_link_libraries(ktma_ubsi_equipment_readiness_test PRIVATE ktma_ubsi_readiness)
    add_test(NAME ktma.ubsi.equipment_readiness COMMAND ktma_ubsi_equipment_readiness_test)

    add_executable(yalk_analog_procedure_test tests/yalk_analog_procedure_test.cpp)
    target_link_libraries(yalk_analog_procedure_test PRIVATE ktma_ubsi_procedures)
    add_test(NAME ktma.ubsi.yalk_analog_procedure COMMAND yalk_analog_procedure_test)

    add_executable(ubsi_backend_contract_test tests/ubsi_backend_contract_test.cpp)
    target_compile_definitions(ubsi_backend_contract_test PRIVATE
        ORBITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(ubsi_backend_contract_test PRIVATE ktma_ubsi_procedures)
    add_test(NAME ktma.ubsi.backend_contract COMMAND ubsi_backend_contract_test)

    add_executable(scenario_runtime_test tests/scenario_runtime_test.cpp)
    target_compile_definitions(scenario_runtime_test PRIVATE
        ORBITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(scenario_runtime_test PRIVATE
        orbita_stand_runtime
        orbita_stand_adapters
        ktma_ubsi_procedures
        miltech_orbita_integration
        orbita_stand_reporting
        Qt6::Core
        Qt6::Sql)
    # This test still contains the plugin ABI contract whose fixture locates
    # ../plugins relative to the executable. Preserve its binary location until
    # the plugin contract is split from the KTMA scenario-runtime regression.
    set_target_properties(scenario_runtime_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/station")
    add_test(NAME ktma.ubsi.scenario_runtime COMMAND scenario_runtime_test)

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
        ktma.ubsi.scenario_runtime
        PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_ktma_mingw_bin}"
    )
endif()
