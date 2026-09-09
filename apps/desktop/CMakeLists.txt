cmake_minimum_required(VERSION 3.20)
project(MilTechStationDesktop)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # Р±РµР· СЌС‚РѕРіРѕ resources.qrc (styles.qss) РЅРµ РїРѕРїР°РґР°РµС‚ РІ Р±РёРЅР°СЂРЅРёРє в†’ С‚РµРјР° РЅРµ РіСЂСѓР·РёС‚СЃСЏ

find_package(Qt6 REQUIRED COMPONENTS Core Widgets PrintSupport Sql Svg SerialPort Concurrent)

add_executable(MilTechStation
    main.cpp
    mainwindow.cpp
    mainwindow.h
    ktma_mainwindow.cpp
    ktma_mainwindow.h
    home_page.cpp
    home_page.h
    registrar_page.cpp
    registrar_page_production.cpp
    registrar_page.h
    test_page.cpp
    test_page.h
    plot_widget.cpp
    plot_widget.h
    qcustomplot.cpp
    qcustomplot.h
    config_manager_widget.cpp
    config_manager_widget.h
    metadata_service.cpp
    metadata_service.h
    tolerance_resolver.h
    tolerance_resolver.cpp
    watch_set_widget.cpp
    watch_set_widget.h
    parameter_browser.cpp
    parameter_browser.h
    encoding_utils.cpp
    encoding_utils.h
    bar_chart_widget.h bar_chart_widget.cpp
    table_widget.h table_widget.cpp
    anomaly_widget.h anomaly_widget.cpp
    category_card_widget.h category_card_widget.cpp
    category_grid_widget.h category_grid_widget.cpp
    detail_view.h detail_view.cpp
    main_page.h main_page.cpp
    resources/resources.qrc
    channel_list_widget.h channel_list_widget.cpp
    readout_widget.h readout_widget.cpp
    channel_status.h
    plot_theme.h
    scenario_yaml_editor.h scenario_yaml_editor.cpp
    adapter_monitor_widget.h adapter_monitor_widget.cpp
    equipment_control_widget.h equipment_control_widget.cpp
)


target_include_directories(MilTechStation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # encoding_utils.h, plot_widget.h Рё С‚.Рґ.
    ${CMAKE_SOURCE_DIR}/orbita/include   # orbita.h
    ${CMAKE_SOURCE_DIR}/deliveries/ktma/registrar/include
)

target_link_libraries(MilTechStation PRIVATE
    orbita
    orbita_stand_adapters
    orbita_stand_runtime
    ktma_registrar
    ktma_ubsi
    ktma_ubsi_reporting
    Qt6::Core
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Sql
    Qt6::Svg
    Qt6::SerialPort
    Qt6::Concurrent
)

target_compile_definitions(MilTechStation PRIVATE
    MILTECHSTATION_VERSION="${CMAKE_PROJECT_VERSION}"
)

if(WIN32)
    set(LUSBAPI_DLL ${CMAKE_SOURCE_DIR}/third_party/lusbapi/Lusbapi64.dll)
    if(EXISTS ${LUSBAPI_DLL})
        add_custom_command(TARGET MilTechStation POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${LUSBAPI_DLL} $<TARGET_FILE_DIR:MilTechStation>
        )
    endif()
endif()

# --- РљРѕРїРёСЂСѓРµРј СЂРµСЃСѓСЂСЃС‹ СЂСЏРґРѕРј СЃ exe ---
set(ADDR_SRC ${CMAKE_SOURCE_DIR}/orbita/config/address)

# Р РµСЃСѓСЂСЃС‹ СЂР°Р±РѕС‡РµРіРѕ РєР°С‚Р°Р»РѕРіР° РѕР±РЅРѕРІР»СЏСЋС‚СЃСЏ РїСЂРё РєР°Р¶РґРѕРј Р·Р°РїСѓСЃРєРµ СЃР±РѕСЂРєРё. Р­С‚Рѕ РІР°Р¶РЅРѕ:
# РёР·РјРµРЅРµРЅРёРµ Р‘Р” РёР»Рё Р°РґСЂРµСЃРЅС‹С… СЃРїРёСЃРєРѕРІ РЅРµ РѕР±СЏР·Р°РЅРѕ РїСЂРёРІРѕРґРёС‚СЊ Рє РїРµСЂРµР»РёРЅРєРѕРІРєРµ exe.
add_custom_target(OrbitaRuntimeResources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt6::SerialPort>
        $<TARGET_FILE_DIR:MilTechStation>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ADDR_SRC}/parameters.db
        $<TARGET_FILE_DIR:MilTechStation>/parameters.db
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:MilTechStation>/address
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${ADDR_SRC}
        $<TARGET_FILE_DIR:MilTechStation>/address
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:MilTechStation>/records
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data/catalog
        $<TARGET_FILE_DIR:MilTechStation>/catalog
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data/profiles
        $<TARGET_FILE_DIR:MilTechStation>/profiles
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data/scenarios
        $<TARGET_FILE_DIR:MilTechStation>/scenarios
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/plugins
        $<TARGET_FILE_DIR:MilTechStation>/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:orbita_equipment_probe>
        $<TARGET_FILE_DIR:MilTechStation>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:orbita_telemetry_probe>
        $<TARGET_FILE_DIR:MilTechStation>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:orbita_ubsi_udp_probe>
        $<TARGET_FILE_DIR:MilTechStation>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:visa_discover>
        $<TARGET_FILE_DIR:MilTechStation>
    COMMENT "Updating MilTechStation runtime database and address files"
)

if(WIN32)
    set_target_properties(MilTechStation PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
add_dependencies(OrbitaRuntimeResources MilTechStation orbita_equipment_plugins
    orbita_equipment_probe orbita_telemetry_probe orbita_ubsi_udp_probe visa_discover)

if(BUILD_TESTING)
    add_executable(test_page_smoke_test
        tests/test_page_smoke_test.cpp
        test_page.cpp
        test_page.h
        equipment_control_widget.cpp
        equipment_control_widget.h
    )

    target_include_directories(test_page_smoke_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/orbita/include
    )

    target_link_libraries(test_page_smoke_test PRIVATE
        orbita_stand_domain
        Qt6::Core
        Qt6::Widgets
    )

    add_test(NAME desktop.test_page_smoke COMMAND test_page_smoke_test -platform offscreen)

    get_filename_component(QT_TEST_PLUGIN_DIR "${Qt6_DIR}/../../../plugins" ABSOLUTE)
    set_tests_properties(desktop.test_page_smoke PROPERTIES
        ENVIRONMENT "QT_PLUGIN_PATH=${QT_TEST_PLUGIN_DIR}"
    )
    if(WIN32)
        set_tests_properties(desktop.test_page_smoke PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endif()


