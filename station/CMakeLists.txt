add_library(orbita_stand_core STATIC
    src/scenario.cpp
)

target_include_directories(orbita_stand_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(orbita_stand_core PUBLIC cxx_std_17)
add_library(stand_core ALIAS orbita_stand_core)

add_library(orbita_stand_domain STATIC
    src/yalk_analog_procedure.cpp
    src/ubsi_procedures.cpp
    src/ubsi_procedures_current.cpp
    src/ubsi_procedures_rokt.cpp
    src/ubsi_procedures_v7.cpp
    src/telemetry_procedures.cpp
)

# Layer the historical implementation, current KTMA overrides and ROKT
# commissioning helpers first. ubsi_procedures_v7.cpp owns the final public
# registerUbsiProcedures() symbol and installs the production V7+ISD YVP alias
# last, so `ubsi.yvp` can never silently fall back to the adapter/ROKT path.
set_source_files_properties(src/ubsi_procedures.cpp PROPERTIES
    COMPILE_DEFINITIONS "registerUbsiProcedures=registerLegacyUbsiProcedures")
set_source_files_properties(src/ubsi_procedures_current.cpp PROPERTIES
    COMPILE_DEFINITIONS "registerUbsiProcedures=registerCurrentUbsiProcedures")
set_source_files_properties(src/ubsi_procedures_rokt.cpp PROPERTIES
    COMPILE_DEFINITIONS "registerUbsiProcedures=registerRoktUbsiProcedures")

target_include_directories(orbita_stand_domain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(orbita_stand_domain PUBLIC cxx_std_17)
target_link_libraries(orbita_stand_domain PUBLIC orbita_stand_core)

find_package(Qt6 REQUIRED COMPONENTS Core Network SerialPort Sql Gui)

add_library(orbita_stand_catalog STATIC
    src/yaml_lite.cpp
    src/catalog.cpp
)
target_include_directories(orbita_stand_catalog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(orbita_stand_catalog PUBLIC cxx_std_17)
target_link_libraries(orbita_stand_catalog PUBLIC yaml-cpp Qt6::Core Qt6::Sql)
add_library(stand_catalog ALIAS orbita_stand_catalog)

add_library(orbita_stand_reporting STATIC
    src/run_store.cpp
    src/report_writer.cpp
)
target_include_directories(orbita_stand_reporting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(orbita_stand_reporting PUBLIC cxx_std_17)
target_link_libraries(orbita_stand_reporting PUBLIC orbita_stand_core Qt6::Core Qt6::Sql Qt6::Gui)
add_library(stand_reporting ALIAS orbita_stand_reporting)

# Universal station runtime. This target deliberately does not depend on the
# KTMA/UBSI procedure domain or reporting. It owns profile loading, component
# lifecycle and equipment/execution runtime only.
add_library(orbita_stand_runtime STATIC
    src/config.cpp
    src/equipment_runtime.cpp
    src/component_runtime.cpp
    src/execution_runtime.cpp
)
target_include_directories(orbita_stand_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(orbita_stand_runtime PUBLIC cxx_std_17)
target_link_libraries(orbita_stand_runtime PUBLIC
    orbita_stand_core orbita_stand_catalog Qt6::Core)

# Hardware/protocol adapters are reusable station IO. Product procedures must
# depend on these adapters when needed, never the other way around.
add_library(orbita_stand_adapters STATIC
    adapters/equipment_adapters.cpp
    adapters/e2010_sample_source.cpp
    adapters/sample_source_registry.cpp
    adapters/v7_visa_voltmeter.cpp
    adapters/visa_instrument.cpp
    adapters/dho_waveform.cpp
    adapters/yalk_frame.cpp
    adapters/ytp_frame.cpp
    adapters/ulk_udp_transport.cpp
)
target_include_directories(orbita_stand_adapters
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/adapters
        ${CMAKE_SOURCE_DIR}/third_party/lusbapi
)
target_compile_features(orbita_stand_adapters PUBLIC cxx_std_17)
target_link_libraries(orbita_stand_adapters
    PUBLIC orbita_stand_runtime Qt6::Core Qt6::Network Qt6::SerialPort
)
if(WIN32)
    target_link_libraries(orbita_stand_adapters PUBLIC
        ws2_32
        ${CMAKE_SOURCE_DIR}/third_party/lusbapi/libLusbapi64.a
    )
endif()
add_library(stand_io ALIAS orbita_stand_adapters)

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

    add_executable(scenario_runtime_test tests/scenario_runtime_test.cpp)
    target_compile_definitions(scenario_runtime_test PRIVATE
        ORBITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(scenario_runtime_test PRIVATE
        orbita_stand_runtime
        orbita_stand_adapters
        orbita_stand_domain
        orbita_stand_reporting
        Qt6::Core
        Qt6::Sql)
    add_test(NAME stand.scenario_runtime COMMAND scenario_runtime_test)

    add_executable(component_profile_test tests/component_profile_test.cpp)
    target_compile_definitions(component_profile_test PRIVATE
        ORBITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(component_profile_test PRIVATE orbita_stand_runtime Qt6::Core)
    add_test(NAME stand.component_profile COMMAND component_profile_test)

    add_executable(component_runtime_test tests/component_runtime_test.cpp)
    target_compile_definitions(component_runtime_test PRIVATE
        ORBITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(component_runtime_test PRIVATE
        orbita_stand_runtime orbita_stand_adapters Qt6::Core)
    add_test(NAME stand.component_runtime COMMAND component_runtime_test)

    add_executable(execution_runtime_test tests/execution_runtime_test.cpp)
    target_link_libraries(execution_runtime_test PRIVATE orbita_stand_runtime Qt6::Core)
    add_test(NAME stand.execution_runtime COMMAND execution_runtime_test)

    add_executable(ubsi_backend_contract_test tests/ubsi_backend_contract_test.cpp)
    target_compile_definitions(ubsi_backend_contract_test PRIVATE
        ORBITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(ubsi_backend_contract_test PRIVATE orbita_stand_domain)
    add_test(NAME stand.ubsi_backend_contract COMMAND ubsi_backend_contract_test)

    if(WIN32)
        set(_orbita_qt_test_path "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        set_tests_properties(
            stand.equipment_protocol
            stand.scenario_runtime
            stand.component_profile
            stand.component_runtime
            stand.execution_runtime
            PROPERTIES ENVIRONMENT_MODIFICATION "${_orbita_qt_test_path}")
    endif()
endif()

function(add_orbita_equipment_plugin target source)
    add_library(${target} MODULE ${source})
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_link_libraries(${target} PRIVATE orbita_stand_adapters orbita_stand_runtime)
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
endfunction()

add_orbita_equipment_plugin(orbita_plugin_ktma_adapter_udp plugins/ktma_adapter_udp_plugin.cpp)
add_orbita_equipment_plugin(orbita_plugin_isd_http plugins/isd_http_plugin.cpp)
add_orbita_equipment_plugin(orbita_plugin_v7_visa plugins/v7_visa_plugin.cpp)
add_orbita_equipment_plugin(orbita_plugin_rigol_generator plugins/rigol_generator_plugin.cpp)
add_orbita_equipment_plugin(orbita_plugin_akip_1160 plugins/power_supply_plugin.cpp)
add_orbita_equipment_plugin(orbita_plugin_rigol_dho8xx plugins/rigol_dho8xx_plugin.cpp)
add_orbita_equipment_plugin(orbita_plugin_tcp_scpi_bench plugins/tcp_scpi_bench_plugin.cpp)

add_custom_target(orbita_equipment_plugins ALL DEPENDS
    orbita_plugin_ktma_adapter_udp
    orbita_plugin_isd_http
    orbita_plugin_v7_visa
    orbita_plugin_rigol_generator
    orbita_plugin_akip_1160
    orbita_plugin_rigol_dho8xx)
add_dependencies(orbita_equipment_plugins orbita_plugin_tcp_scpi_bench)

add_executable(catalog_import tools/catalog_import.cpp)
target_link_libraries(catalog_import PRIVATE orbita_stand_runtime Qt6::Core)

add_executable(orbita_equipment_probe tools/equipment_probe.cpp)
target_link_libraries(orbita_equipment_probe PRIVATE orbita_stand_runtime Qt6::Core)

add_executable(orbita_telemetry_probe tools/orbita_telemetry_probe.cpp)
target_link_libraries(orbita_telemetry_probe PRIVATE orbita)

add_executable(orbita_ubsi_udp_probe tools/ubsi_udp_probe.cpp)
target_link_libraries(orbita_ubsi_udp_probe PRIVATE orbita_stand_adapters)

if(MINGW)
    target_link_options(orbita_ubsi_udp_probe PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

if(WIN32)
    add_executable(orbita_ubsi_rokt_probe
        tools/ubsi_rokt_probe.cpp
    )
    target_link_libraries(orbita_ubsi_rokt_probe PRIVATE ws2_32)
    if(MINGW)
        target_link_options(orbita_ubsi_rokt_probe PRIVATE
            -static -static-libgcc -static-libstdc++)
    endif()
endif()

if(WIN32)
    add_executable(orbita_ytp_rokt_probe
        tools/ytp_rokt_probe.cpp
    )
    target_link_libraries(orbita_ytp_rokt_probe PRIVATE ws2_32)
    if(MINGW)
        target_link_options(orbita_ytp_rokt_probe PRIVATE
            -static -static-libgcc -static-libstdc++)
    endif()
endif()

if(WIN32)
    add_executable(orbita_yvp_rokt_probe
        tools/yvp_rokt_probe.cpp
    )
    target_link_libraries(orbita_yvp_rokt_probe PRIVATE orbita_stand_adapters)
    if(MINGW)
        target_link_options(orbita_yvp_rokt_probe PRIVATE
            -static -static-libgcc -static-libstdc++)
    endif()
endif()

add_executable(yalk_timing_probe tools/yalk_timing_probe.cpp)
target_link_libraries(yalk_timing_probe PRIVATE orbita_stand_adapters)

add_executable(yalk_full_probe tools/yalk_full_probe.cpp)
target_link_libraries(yalk_full_probe PRIVATE orbita_stand_adapters)

if(WIN32)
    add_executable(visa_discover tools/visa_discover.cpp)
endif()

if(WIN32)
    add_executable(v7_probe tools/v7_probe.cpp)
    target_link_libraries(v7_probe PRIVATE orbita_stand_adapters)
    if(MINGW)
        target_link_options(v7_probe PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()
