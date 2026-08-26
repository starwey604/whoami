add_executable(whoami_vm_host main.cpp)
target_link_libraries(whoami_vm_host PRIVATE whoami_vm)
target_include_directories(whoami_vm_host PRIVATE ${PROJECT_SOURCE_DIR}/include)
