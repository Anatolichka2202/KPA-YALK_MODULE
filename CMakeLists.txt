cmake_minimum_required(VERSION 3.16)
project(liborbita LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Статическая библиотека
add_library(liborbita STATIC
    src/address/address_parser.cpp
    src/address/address_manager.cpp
    src/core/orbita_core.cpp
    src/core/data_pool.cpp
    src/core/thread_pool.cpp
    src/core/logger.cpp
    src/decoder/fifo_buffer.cpp
    src/decoder/frame_decoder_m16.cpp
    src/decoder/decoder_factory.cpp
    src/decoder/frame_decoder_m16.cpp

    src/device/e2010_device.cpp
    src/device/limesdr_device.cpp
    src/device/visa_device.cpp
)

target_include_directories(liborbita PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(liborbita PRIVATE
    ${CMAKE_THREAD_LIBS_INIT}
)
# Тестовая программа
add_executable(test_decoder_m16_from_tlm
    tests/test_decoder_m16_from_tlm.cpp
)
target_link_libraries(test_decoder_m16_from_tlm liborbita)
