cmake_minimum_required(VERSION 3.31)
project(whoami VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(FindDependencies)

add_executable(${PROJECT_NAME})

add_subdirectory(src)
add_subdirectory(tools/vm_host)
add_subdirectory(samples/emulator)

add_dependencies(${PROJECT_NAME} whoami_vm_host)
target_compile_definitions(${PROJECT_NAME} PRIVATE
        WHOAMI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        WHOAMI_VM_HOST_PATH="$<TARGET_FILE:whoami_vm_host>"
)

include(GrepConfigs)
include(GrepAssets)
