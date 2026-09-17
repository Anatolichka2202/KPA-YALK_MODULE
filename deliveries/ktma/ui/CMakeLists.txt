find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(ktma_delivery_ui STATIC
    include/ktma/ui/delivery_manifest.h
    include/ktma/ui/product_ui_module.h
    src/delivery_manifest.cpp
)

target_include_directories(ktma_delivery_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(ktma_delivery_ui PUBLIC cxx_std_17)
target_link_libraries(ktma_delivery_ui PUBLIC miltech_ui_contract Qt6::Core)
