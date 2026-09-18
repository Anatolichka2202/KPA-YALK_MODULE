cmake_minimum_required(VERSION 3.20)
project(MilTechStationDesktop)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets PrintSupport Sql Svg SerialPort Concurrent)
add_compile_options("-include${CMAKE_CURRENT_SOURCE_DIR}/qt_size_compat.h")

add_executable(MilTechStation
    main.cpp
    mainwindow.cpp
    mainwindow.h
    ktma_mainwindow.cpp
    ktma_mainwindow.h
    home_page.cpp
    home_page.h
    registrar_page_clean.cpp
    registrar_page_production.cpp
    registrar_page.h
    test_page_final.cpp
    test_page.h
    production_prototype_port.cpp
    ubsi_ui_model.h
    ubsi_measurement_views.h
    ubsi_view_controls.cpp
    tu_flow_widget_clean.cpp
    tu_flow_widget.h
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
    watch_set_widget.h
    watch_set_widget.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/orbita/include
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

set(ADDR_SRC ${CMAKE_SOURCE_DIR}/orbita/config/address)

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
    orbita_equipment_probe orbita_telemetry_probe orbita_ubsi_udp_probe
    visa_discover)

if(BUILD_TESTING)
    set(UBSI_UI_TEST_SOURCES
        test_page_final.cpp
        test_page.h
        production_prototype_port.cpp
        ubsi_ui_model.h
        ubsi_measurement_views.h
        ubsi_view_controls.cpp
        tu_flow_widget_clean.cpp
        tu_flow_widget.h
        equipment_control_widget.cpp
        equipment_control_widget.h
    )

    add_executable(ubsi_ui_model_background_test
        tests/ubsi_ui_model_background_test.cpp
        ubsi_ui_model.h
    )
    target_include_directories(ubsi_ui_model_background_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/station/include
    )
    target_link_libraries(ubsi_ui_model_background_test PRIVATE Qt6::Core)
    add_test(NAME desktop.ubsi_ui_model_background COMMAND ubsi_ui_model_background_test)

    add_executable(test_page_smoke_test
        tests/test_page_prototype_acceptance.cpp
        ${UBSI_UI_TEST_SOURCES}
    )
    target_include_directories(test_page_smoke_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/orbita/include
    )
    target_link_libraries(test_page_smoke_test PRIVATE
        orbita_stand_domain Qt6::Core Qt6::Widgets)
    add_test(NAME desktop.test_page_smoke COMMAND test_page_smoke_test -platform offscreen)

    add_executable(test_page_yvp_event_test
        tests/test_page_yvp_event_test.cpp
        ${UBSI_UI_TEST_SOURCES}
    )
    target_include_directories(test_page_yvp_event_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/orbita/include
    )
    target_link_libraries(test_page_yvp_event_test PRIVATE
        orbita_stand_domain Qt6::Core Qt6::Widgets)
    add_test(NAME desktop.test_page_yvp_event COMMAND test_page_yvp_event_test -platform offscreen)

    add_executable(test_page_power_yalk_event_test
        tests/test_page_power_yalk_event_test.cpp
        ${UBSI_UI_TEST_SOURCES}
    )
    target_include_directories(test_page_power_yalk_event_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/orbita/include
    )
    target_link_libraries(test_page_power_yalk_event_test PRIVATE
        orbita_stand_domain Qt6::Core Qt6::Widgets)
    add_test(NAME desktop.test_page_power_yalk_event COMMAND test_page_power_yalk_event_test -platform offscreen)

    add_executable(test_page_tu_runtime_test
        tests/test_page_tu_runtime_test.cpp
        ${UBSI_UI_TEST_SOURCES}
    )
    target_include_directories(test_page_tu_runtime_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/orbita/include
    )
    target_link_libraries(test_page_tu_runtime_test PRIVATE
        orbita_stand_domain Qt6::Core Qt6::Widgets)
    add_test(NAME desktop.test_page_tu_runtime COMMAND test_page_tu_runtime_test -platform offscreen)

    add_executable(tu_flow_widget_test
        tests/tu_flow_widget_test.cpp
        tu_flow_widget_clean.cpp
        tu_flow_widget.h
    )
    target_include_directories(tu_flow_widget_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(tu_flow_widget_test PRIVATE Qt6::Core Qt6::Widgets)
    add_test(NAME desktop.tu_flow_widget COMMAND tu_flow_widget_test -platform offscreen)

    get_filename_component(QT_TEST_PLUGIN_DIR "${Qt6_DIR}/../../../plugins" ABSOLUTE)
    set_tests_properties(
        desktop.ubsi_ui_model_background
        desktop.test_page_smoke
        desktop.test_page_yvp_event
        desktop.test_page_power_yalk_event
        desktop.test_page_tu_runtime
        desktop.tu_flow_widget
        PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${QT_TEST_PLUGIN_DIR}"
    )
    if(WIN32)
        set_tests_properties(
            desktop.ubsi_ui_model_background
            desktop.test_page_smoke
            desktop.test_page_yvp_event
            desktop.test_page_power_yalk_event
            desktop.test_page_tu_runtime
            desktop.tu_flow_widget
            PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endif()
