cmake_minimum_required(VERSION 3.31)
project(whoami VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(FindDependencies)

add_executable(${PROJECT_NAME})

add_subdirectory(src)

include(GrepConfigs)
include(GrepAssets)


