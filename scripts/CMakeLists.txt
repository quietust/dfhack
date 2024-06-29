include(Scripts.cmake)

install(DIRECTORY ${dfhack_SOURCE_DIR}/scripts
        DESTINATION ${DFHACK_DATA_DESTINATION}
        FILES_MATCHING PATTERN "*.lua"
                       PATTERN "*.rb"
        )
