cmake_minimum_required(VERSION 3.31)
project(whoami_emulator_sample LANGUAGES C CXX)

if(NOT UNIX)
    message(FATAL_ERROR "The terminal sample currently requires a POSIX host.")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(WHOAMI_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(TINYEMU_DIR "${WHOAMI_SOURCE_DIR}/3rdparty/tinyemu")

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
target_link_libraries(tinyemu_riscv PUBLIC rt)

add_library(whoami_vm STATIC "${WHOAMI_SOURCE_DIR}/src/vm/vm.cpp")
target_include_directories(whoami_vm PUBLIC "${WHOAMI_SOURCE_DIR}/include")
target_link_libraries(whoami_vm PUBLIC tinyemu_riscv)

add_executable(whoami_emulator_sample main.cpp)
target_link_libraries(whoami_emulator_sample PRIVATE whoami_vm)
target_compile_definitions(whoami_emulator_sample PRIVATE
        WHOAMI_SOURCE_DIR="${WHOAMI_SOURCE_DIR}"
)
