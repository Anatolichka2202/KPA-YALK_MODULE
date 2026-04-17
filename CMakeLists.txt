cmake_minimum_required(VERSION 3.16)
project(liborbita LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)   # для Qt-классов (например, E2010Device)

# Поиск Qt (Core нужен всегда, Test – только для тестов)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

option(BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------
# Статическая библиотека
# ---------------------------------------------------------------------
set(LIBORBITA_SOURCES
    src/address/address_parser.cpp
    src/address/address_manager.cpp
    src/core/orbita_core.cpp
    src/core/data_pool.cpp
    src/core/logger.cpp
    src/decoder/fifo_buffer.cpp
    src/decoder/frame_decoder_m16.cpp
    src/decoder/frame_decoder_m8.cpp
    src/decoder/decoder_factory.cpp
    src/device/e2010_device.cpp
    #src/device/limesdr_device.cpp
    #src/device/visa_device.cpp
    src/value/value_converter.cpp
    src/tlm/tlm_writer.cpp
    src/tlm/tlm_reader.cpp
    src/core/orbita.cpp
)

add_library(liborbita STATIC ${LIBORBITA_SOURCES}
    src/decoder/bitstream_recoverer.h src/decoder/bitstream_recoverer.cpp)

target_compile_definitions(liborbita PRIVATE DEBUG_RECOVERER) # условная компиляция

target_include_directories(liborbita PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(liborbita PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    ${CMAKE_THREAD_LIBS_INIT}
)

# ---------------------------------------------------------------------
# Тесты (только если BUILD_TESTS=ON)
# ---------------------------------------------------------------------
if(BUILD_TESTS)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Core)

    file(GLOB TEST_SOURCES "tests/test_*.cpp")

    foreach(test_src ${TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        # Отключаем AUTOMOC для тестовых файлов
        set_property(SOURCE ${test_src} PROPERTY SKIP_AUTOMOC ON)
        target_link_libraries(${test_name}
            PRIVATE
                liborbita
                Qt${QT_VERSION_MAJOR}::Test
                Qt${QT_VERSION_MAJOR}::Core
        )
        target_include_directories(${test_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()

    enable_testing()
endif()
