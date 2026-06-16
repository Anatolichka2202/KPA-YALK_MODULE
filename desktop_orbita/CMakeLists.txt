cmake_minimum_required(VERSION 3.20)
project(OrbitaDesktop)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets PrintSupport Sql)

add_executable(OrbitaDesktop
    main.cpp
    mainwindow.cpp
    plot_widget.cpp
    plot_widget.h
    qcustomplot.cpp
    qcustomplot.h
    config_manager_widget.cpp
    config_manager_widget.h
    metadata_service.cpp
    metadata_service.h
    parameter_browser.cpp
    parameter_browser.h
    encoding_utils.cpp
    encoding_utils.h
)

target_include_directories(OrbitaDesktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # encoding_utils.h, plot_widget.h и т.д.
    ${CMAKE_SOURCE_DIR}/orbita/include   # orbita.h
)

target_link_libraries(OrbitaDesktop PRIVATE
    orbita
    Qt6::Core
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Sql
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

# parameters.db
add_custom_command(TARGET OrbitaDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ADDR_SRC}/parameters.db
        $<TARGET_FILE_DIR:OrbitaDesktop>/parameters.db
    COMMENT "Copying parameters.db"
)

# Все .txt-конфиги в подпапку address/
add_custom_command(TARGET OrbitaDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:OrbitaDesktop>/address
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${ADDR_SRC}
        $<TARGET_FILE_DIR:OrbitaDesktop>/address
    COMMENT "Copying address config files"
)

# Создаём папку для записей TLM
add_custom_command(TARGET OrbitaDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:OrbitaDesktop>/records
    COMMENT "Creating records directory"
)
