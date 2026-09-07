# Packaging/install rules only. Product behaviour must not live here.

if(NOT WIN32)
    message(STATUS "MilTech Station Windows release packaging is disabled on non-Windows hosts")
    return()
endif()

set(MILTECH_STAGE_APP_DIR "app")

install(TARGETS OrbitaDesktop
    RUNTIME DESTINATION "${MILTECH_STAGE_APP_DIR}"
    COMPONENT Runtime
)

# Diagnostic/runtime tools shipped with the station build when those targets exist.
foreach(MILTECH_TOOL_TARGET IN ITEMS
    orbita_equipment_probe
    orbita_telemetry_probe
    orbita_ubsi_udp_probe
    visa_discover
)
    if(TARGET ${MILTECH_TOOL_TARGET})
        install(TARGETS ${MILTECH_TOOL_TARGET}
            RUNTIME DESTINATION "${MILTECH_STAGE_APP_DIR}"
            COMPONENT Runtime
        )
    endif()
endforeach()

# Build provenance belongs to the immutable application payload.
install(FILES "${CMAKE_BINARY_DIR}/build-info.json"
    DESTINATION "${MILTECH_STAGE_APP_DIR}"
    COMPONENT Runtime
)

# Keep the current runtime layout compatible while the ProgramData migration is
# introduced as a separate release-engineering slice. These are release seeds,
# not a declaration that mutable data should remain beside the executable.
install(FILES
    "${CMAKE_SOURCE_DIR}/desktop_orbita/resources/stand.ini"
    "${CMAKE_SOURCE_DIR}/orbita/config/address/parameters.db"
    DESTINATION "${MILTECH_STAGE_APP_DIR}"
    COMPONENT KtmaPayload
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/orbita/config/address/"
    DESTINATION "${MILTECH_STAGE_APP_DIR}/address"
    COMPONENT KtmaPayload
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/catalog/"
    DESTINATION "${MILTECH_STAGE_APP_DIR}/catalog"
    COMPONENT KtmaPayload
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/profiles/"
    DESTINATION "${MILTECH_STAGE_APP_DIR}/profiles"
    COMPONENT KtmaPayload
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/scenarios/"
    DESTINATION "${MILTECH_STAGE_APP_DIR}/scenarios"
    COMPONENT KtmaPayload
)

# Equipment plugins are built into ${CMAKE_BINARY_DIR}/plugins by stand/CMakeLists.txt.
# OPTIONAL keeps configure/install usable for partial developer builds.
install(DIRECTORY "${CMAKE_BINARY_DIR}/plugins/"
    DESTINATION "${MILTECH_STAGE_APP_DIR}/plugins"
    COMPONENT Runtime
    OPTIONAL
)

# Vendor bridge DLL already tracked by the repository. Other vendor runtimes and
# drivers are prerequisites and are intentionally not guessed or bundled here.
set(MILTECH_LUSBAPI_DLL "${CMAKE_SOURCE_DIR}/third_party/lusbapi/Lusbapi64.dll")
if(EXISTS "${MILTECH_LUSBAPI_DLL}")
    install(FILES "${MILTECH_LUSBAPI_DLL}"
        DESTINATION "${MILTECH_STAGE_APP_DIR}"
        COMPONENT Runtime
    )
endif()
