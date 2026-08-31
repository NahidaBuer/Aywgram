# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(test_text WIN32)
init_target(test_text "(tests)")

target_include_directories(test_text PRIVATE ${src_loc})

nice_target_sources(test_text ${src_loc}
PRIVATE
    tests/test_main.cpp
    tests/test_main.h
    tests/test_text.cpp
)

nice_target_sources(test_text ${res_loc}
PRIVATE
    qrc/emoji_1.qrc
    qrc/emoji_2.qrc
    qrc/emoji_3.qrc
    qrc/emoji_4.qrc
    qrc/emoji_5.qrc
    qrc/emoji_6.qrc
    qrc/emoji_7.qrc
    qrc/emoji_8.qrc
)

target_link_libraries(test_text
PRIVATE
    desktop-app::lib_base
    desktop-app::lib_crl
    desktop-app::lib_ui
    desktop-app::external_qt
    desktop-app::external_qt_static_plugins
)

set_target_properties(test_text PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_dependencies(Telegram test_text)

target_prepare_qrc(test_text)

if (APPLE)
    add_custom_command(TARGET test_text POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:test_text>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/test_text.rcc"
            "${CMAKE_BINARY_DIR}/lib_ui.rcc"
            "$<TARGET_FILE_DIR:test_text>/Contents/Resources/"
    )
endif()

add_executable(test_session_transfer_codec EXCLUDE_FROM_ALL)
init_target(test_session_transfer_codec "(tests)")

target_include_directories(test_session_transfer_codec PRIVATE ${src_loc})

nice_target_sources(test_session_transfer_codec ${src_loc}
PRIVATE
    ayu/session_transfer/session_transfer_codec.cpp
    ayu/session_transfer/session_transfer_codec.h
    tests/test_session_transfer_codec.cpp
)

target_link_libraries(test_session_transfer_codec
PRIVATE
    desktop-app::lib_base
    desktop-app::external_qt
)

set_target_properties(test_session_transfer_codec PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_executable(test_message_search_filter EXCLUDE_FROM_ALL)
init_target(test_message_search_filter "(tests)")

target_include_directories(test_message_search_filter PRIVATE ${src_loc})
target_precompile_headers(test_message_search_filter PRIVATE
    ${src_loc}/mtproto/mtproto_pch.h
)

nice_target_sources(test_message_search_filter ${src_loc}
PRIVATE
    api/api_messages_search_filter.cpp
    api/api_messages_search.h
    api/api_messages_search_intersection_state.cpp
    api/api_messages_search_intersection_state.h
    api/api_messages_search_state.cpp
    api/api_messages_search_state.h
    mtproto/details/mtproto_dump_to_text.cpp
    tests/test_message_search_filter.cpp
)

target_link_libraries(test_message_search_filter
PRIVATE
    tdesktop::td_scheme
    desktop-app::lib_base
    desktop-app::lib_ui
    desktop-app::lib_tl
    desktop-app::external_qt
    desktop-app::external_zlib
)

set_target_properties(test_message_search_filter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_executable(test_ayu_cloud_codec EXCLUDE_FROM_ALL)
init_target(test_ayu_cloud_codec "(tests)")

target_include_directories(test_ayu_cloud_codec PRIVATE ${src_loc})

nice_target_sources(test_ayu_cloud_codec ${src_loc}
PRIVATE
    ayu/cloud/ayu_cloud_codec.cpp
    ayu/cloud/ayu_cloud_codec.h
    tests/test_ayu_cloud_codec.cpp
)

target_link_libraries(test_ayu_cloud_codec
PRIVATE
    desktop-app::lib_base
    desktop-app::external_qt
    desktop-app::external_zlib
)

set_target_properties(test_ayu_cloud_codec PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_executable(test_update_metadata EXCLUDE_FROM_ALL)
init_target(test_update_metadata "(tests)")

target_include_directories(test_update_metadata PRIVATE ${src_loc})

nice_target_sources(test_update_metadata ${src_loc}
PRIVATE
    core/update_metadata.cpp
    core/update_metadata.h
    tests/test_update_metadata.cpp
)

target_link_libraries(test_update_metadata
PRIVATE
    desktop-app::lib_base
    desktop-app::external_qt
)

set_target_properties(test_update_metadata PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
