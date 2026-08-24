if(NOT UNIX)
    message(FATAL_ERROR "The terminal sample currently requires a POSIX host.")
endif()

add_executable(whoami_emulator_sample main.cpp)
target_link_libraries(whoami_emulator_sample PRIVATE whoami_vm)
target_compile_definitions(whoami_emulator_sample PRIVATE
        WHOAMI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
