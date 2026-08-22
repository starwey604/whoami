file(GLOB_RECURSE GAME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
target_sources(${PROJECT_NAME} PUBLIC ${GAME_SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(${PROJECT_NAME} PUBLIC
        SDL3::SDL3 SDL3_image::SDL3_image SDL3_sound::SDL3_sound SDL3_ttf::SDL3_ttf
        spdlog::spdlog tomlplusplus::tomlplusplus
)
