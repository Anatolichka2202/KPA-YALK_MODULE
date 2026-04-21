cmake_minimum_required(VERSION 3.16)
project(liborbita LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)

# ---------------------------------------------------------------------
# Опции
# ---------------------------------------------------------------------
option(BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------
# Поиск Qt
# ---------------------------------------------------------------------
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

if(BUILD_TESTS)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
endif()

# ---------------------------------------------------------------------
# Пути к заголовкам и библиотекам Lusbapi
# ---------------------------------------------------------------------
set(LUSBAPI_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lusbapi)
set(LUSBAPI_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lusbapi/libLusbapi64.a)

if(NOT EXISTS ${LUSBAPI_LIBRARY})
    message(WARNING "Lusbapi library not found at ${LUSBAPI_LIBRARY}")
endif()

# ---------------------------------------------------------------------
# Исходные файлы библиотеки (без тестов)
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
    src/value/value_converter.cpp
    src/tlm/tlm_writer.cpp
    src/tlm/tlm_reader.cpp
    src/core/orbita.cpp
    src/decoder/bitstream_recoverer.cpp
)

add_library(liborbita STATIC ${LIBORBITA_SOURCES}
  )

target_compile_definitions(liborbita PRIVATE DEBUG_RECOVERER)

target_include_directories(liborbita PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(liborbita PRIVATE
    ${LUSBAPI_INCLUDE_DIR}
)

target_link_libraries(liborbita PRIVATE
    ${LUSBAPI_LIBRARY}
    Qt${QT_VERSION_MAJOR}::Core
)

# Дополнительные системные библиотеки для Windows (требуются Lusbapi)
if(WIN32)
    target_link_libraries(liborbita PRIVATE winmm ws2_32)
endif()

# ---------------------------------------------------------------------
# Тесты
# ---------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()
    target_compile_definitions(liborbita PUBLIC TESTING)

    file(GLOB TEST_SOURCES "tests/test_*.cpp")

    foreach(test_src ${TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})

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

        # Копируем DLL
        if(WIN32 AND EXISTS ${LUSBAPI_LIBRARY})
            get_filename_component(LUSBAPI_DLL_DIR ${LUSBAPI_LIBRARY} DIRECTORY)
            set(LUSBAPI_DLL ${LUSBAPI_DLL_DIR}/Lusbapi64.dll)
            if(EXISTS ${LUSBAPI_DLL})
                add_custom_command(TARGET ${test_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${LUSBAPI_DLL} $<TARGET_FILE_DIR:${test_name}>
                )
            endif()
        endif()

        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
