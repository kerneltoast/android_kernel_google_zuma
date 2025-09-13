set(SOURCES
    src/fwupdater.c
    tests/fwupdater_unit_tests.c
    tests/comms_tests.c
    tests/fwpkg_tests.c
)

add_library(fwupdater ${SOURCES})

target_include_directories(fwupdater PUBLIC
    include
)

target_link_libraries(fwupdater
    PRIVATE
        qmrom
)
