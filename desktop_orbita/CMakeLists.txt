cmake_minimum_required(VERSION 3.20)
project(OrbitaDesktop)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # без этого resources.qrc (styles.qss) не попадает в бинарник → тема не грузится

find_package(Qt6 REQUIRED COMPONENTS Core Widgets PrintSupport Sql Svg SerialPort)

add_executable(OrbitaDesktop
    main.cpp
    mainwindow.cpp
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
    scenario_model.h
    scenario_io.h scenario_io.cpp
    scenario_wizard.h scenario_wizard.cpp
    channel_picker_dialog.h channel_picker_dialog.cpp
)


target_include_directories(OrbitaDesktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # encoding_utils.h, plot_widget.h и т.д.
    ${CMAKE_SOURCE_DIR}/orbita/include   # orbita.h
)

target_link_libraries(OrbitaDesktop PRIVATE
    orbita
    orbita_stand_adapters
    Qt6::Core
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Sql
    Qt6::Svg
    Qt6::SerialPort
)

if(WIN32)
    set(LUSBAPI_DLL ${CMAKE_SOURCE_DIR}/third_party/lusbapi/Lusbapi64.dll)
    if(EXISTS ${LUSBAPI_DLL})
        add_custom_command(TARGET OrbitaDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${LUSBAPI_DLL} $<TARGET_FILE_DIR:OrbitaDesktop>
        )
    endif()
endif()

# --- Копируем ресурсы рядом с exe ---
set(ADDR_SRC ${CMAKE_SOURCE_DIR}/orbita/config/address)

# Ресурсы рабочего каталога обновляются при каждом запуске сборки. Это важно:
# изменение БД или адресных списков не обязано приводить к перелинковке exe.
add_custom_target(OrbitaRuntimeResources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt6::SerialPort>
        $<TARGET_FILE_DIR:OrbitaDesktop>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/stand.ini
        $<TARGET_FILE_DIR:OrbitaDesktop>/stand.ini
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ADDR_SRC}/parameters.db
        $<TARGET_FILE_DIR:OrbitaDesktop>/parameters.db
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:OrbitaDesktop>/address
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${ADDR_SRC}
        $<TARGET_FILE_DIR:OrbitaDesktop>/address
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:OrbitaDesktop>/records
    COMMENT "Updating OrbitaDesktop runtime database and address files"
)

if(WIN32)
    set_target_properties(OrbitaDesktop PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
add_dependencies(OrbitaRuntimeResources OrbitaDesktop)

if(BUILD_TESTING)
    add_executable(test_page_smoke_test
        tests/test_page_smoke_test.cpp
        test_page.cpp
        test_page.h
    )
    target_include_directories(test_page_smoke_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_page_smoke_test PRIVATE Qt6::Core Qt6::Widgets)
    if(WIN32)
        add_test(NAME desktop.test_page_smoke COMMAND test_page_smoke_test)
    else()
        add_test(NAME desktop.test_page_smoke COMMAND test_page_smoke_test -platform offscreen)
    endif()
    get_filename_component(QT_TEST_PLUGIN_DIR "${Qt6_DIR}/../../../plugins" ABSOLUTE)
    set_tests_properties(desktop.test_page_smoke PROPERTIES
        ENVIRONMENT "QT_PLUGIN_PATH=${QT_TEST_PLUGIN_DIR}")
endif()
