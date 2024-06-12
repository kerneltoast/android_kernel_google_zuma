set(SOURCES
    src/qmrom_common.c
    src/qmrom_a0.c
    src/qmrom_b0.c
    src/qmrom_c0.c
    src/qmrom_log.c
)

add_library(qmrom SHARED ${SOURCES})

target_include_directories(qmrom PUBLIC
    include
)