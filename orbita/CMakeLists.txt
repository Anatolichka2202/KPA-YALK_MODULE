cmake_minimum_required(VERSION 3.20)
project(orbita VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)

add_library(orbita STATIC
    orbita_core/orbita.cpp
    orbita_core/orbita_core.cpp
    orbita_core/channel_registry.cpp
    orbita_core/logger.cpp
    decoder/frame_decoder_m16.cpp
    decoder/bitstream_recover.cpp
    decoder/fifo_buffer.cpp
    # decoder/decoder_factory.cpp  # отключено: frame_decoder_m8 ещё не реализован
    address/address_parser.cpp
    address/address_manager.cpp
    value/value_converter.cpp
    device/e2010_device.cpp
    tlm/tlm_writer.cpp
)

target_include_directories(orbita
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include        # orbita.h для потребителей
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}                # decoder/, address/, orbita_core/, ...
        ${CMAKE_SOURCE_DIR}/third_party/lusbapi    # Lusbapi.h
)

if(WIN32)
    target_link_libraries(orbita PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/lusbapi/libLusbapi64.a
        ws2_32
        winmm
    )
endif()

option(ORBITA_BUILD_TESTS "Build unit tests" OFF)
if(ORBITA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
