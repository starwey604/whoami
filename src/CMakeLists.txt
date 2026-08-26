file(GLOB_RECURSE GAME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM GAME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/vm/vm.cpp")
target_sources(${PROJECT_NAME} PUBLIC ${GAME_SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/include)

set(TINYEMU_DIR "${PROJECT_SOURCE_DIR}/3rdparty/tinyemu")

add_library(tinyemu_riscv_cpu32 OBJECT "${TINYEMU_DIR}/riscv_cpu.c")
add_library(tinyemu_riscv_cpu64 OBJECT "${TINYEMU_DIR}/riscv_cpu.c")

foreach(target tinyemu_riscv_cpu32 tinyemu_riscv_cpu64)
    target_include_directories(${target} PRIVATE "${TINYEMU_DIR}")
    target_compile_definitions(${target} PRIVATE
            _GNU_SOURCE
            _FILE_OFFSET_BITS=64
            _LARGEFILE_SOURCE
            CONFIG_RISCV_MAX_XLEN=64
    )
endforeach()

target_compile_definitions(tinyemu_riscv_cpu32 PRIVATE MAX_XLEN=32)
target_compile_definitions(tinyemu_riscv_cpu64 PRIVATE MAX_XLEN=64)

add_library(tinyemu_riscv STATIC
        "${TINYEMU_DIR}/cutils.c"
        "${TINYEMU_DIR}/fs.c"
        "${TINYEMU_DIR}/iomem.c"
        "${TINYEMU_DIR}/json.c"
        "${TINYEMU_DIR}/machine.c"
        "${TINYEMU_DIR}/pci.c"
        "${TINYEMU_DIR}/riscv_machine.c"
        "${TINYEMU_DIR}/simplefb.c"
        "${TINYEMU_DIR}/softfp.c"
        "${TINYEMU_DIR}/virtio.c"
        "${TINYEMU_DIR}/ide.c"
        "${TINYEMU_DIR}/pckbd.c"
        "${TINYEMU_DIR}/ps2.c"
        "${TINYEMU_DIR}/vga.c"
        "${TINYEMU_DIR}/vmmouse.c"
        "${TINYEMU_DIR}/x86_cpu.c"
        "${TINYEMU_DIR}/x86_machine.c"
        $<TARGET_OBJECTS:tinyemu_riscv_cpu32>
        $<TARGET_OBJECTS:tinyemu_riscv_cpu64>
)
target_include_directories(tinyemu_riscv PUBLIC "${TINYEMU_DIR}")
target_compile_definitions(tinyemu_riscv PRIVATE
        _GNU_SOURCE
        _FILE_OFFSET_BITS=64
        _LARGEFILE_SOURCE
        CONFIG_RISCV_MAX_XLEN=64
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(tinyemu_riscv PUBLIC rt)
endif()

add_library(whoami_vm STATIC vm/vm.cpp)
target_include_directories(whoami_vm PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(whoami_vm PUBLIC tinyemu_riscv)

target_link_libraries(${PROJECT_NAME} PUBLIC
        SDL3::SDL3 SDL3_image::SDL3_image SDL3_sound::SDL3_sound SDL3_ttf::SDL3_ttf
        SDL3_shadercross::SDL3_shadercross PkgConfig::LIBTSM
        spdlog::spdlog tomlplusplus::tomlplusplus
        stdc++exp
)
