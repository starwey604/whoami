file(GLOB_RECURSE LOOM_ASSET_FILES
        "${PROJECT_SOURCE_DIR}/assets/*"
)

foreach(src IN LISTS LOOM_ASSET_FILES)
  file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}/assets" "${src}")
  set(dst "${CMAKE_CURRENT_BINARY_DIR}/assets/${rel}")
  configure_file("${src}" "${dst}" COPYONLY)
endforeach()

# 让 CMake 在 configure 阶段就知道这些文件是依赖，
# 任何文件变动都会触发重新 configure（进而重新拷贝）。
set_property(DIRECTORY "${PROJECT_SOURCE_DIR}"
        APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${LOOM_ASSET_FILES}
)
