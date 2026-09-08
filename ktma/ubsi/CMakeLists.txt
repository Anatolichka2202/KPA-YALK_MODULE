add_library(ktma_ubsi STATIC
    include/ktma/ubsi/production.h
    src/production.cpp
)

target_include_directories(ktma_ubsi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(ktma_ubsi PUBLIC cxx_std_17)
target_link_libraries(ktma_ubsi PUBLIC ktma_registrar)

if(BUILD_TESTING)
    add_executable(ktma_ubsi_production_test
        tests/production_test.cpp
    )
    target_compile_definitions(ktma_ubsi_production_test PRIVATE
        KTMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(ktma_ubsi_production_test PRIVATE ktma_ubsi)
    add_test(NAME ktma.ubsi.production COMMAND ktma_ubsi_production_test)
endif()
