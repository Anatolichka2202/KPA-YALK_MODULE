find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(miltech_ui_contract INTERFACE)
target_include_directories(miltech_ui_contract
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(miltech_ui_contract INTERFACE cxx_std_17)
target_link_libraries(miltech_ui_contract INTERFACE Qt6::Core)
