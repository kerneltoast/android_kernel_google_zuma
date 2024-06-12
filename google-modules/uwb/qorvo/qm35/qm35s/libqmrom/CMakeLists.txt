set(SOURCES
    src/qm357xx_rom_b0.c
    src/qm357xx_rom_c0.c
    src/qm357xx_rom_common.c
    src/qmrom_common.c
    src/qmrom_log.c
)

add_library(qmrom ${SOURCES})

target_include_directories(qmrom PUBLIC
    include
)
