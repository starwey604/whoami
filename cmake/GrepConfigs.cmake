file(GLOB_RECURSE LOOM_CONFIG_FILES
        "${PROJECT_SOURCE_DIR}/configs/*"
)

foreach(src IN LISTS LOOM_CONFIG_FILES)
    file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}/configs" "${src}")
    set(dst "${CMAKE_CURRENT_BINARY_DIR}/configs/${rel}")
    configure_file("${src}" "${dst}" COPYONLY)
endforeach()

set_property(DIRECTORY "${PROJECT_SOURCE_DIR}"
        APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${LOOM_CONFIG_FILES}
)