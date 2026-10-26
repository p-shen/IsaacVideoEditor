cmake_minimum_required(VERSION 3.21)
project(IsaacVideoEditor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Quick Qml QuickControls2)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MLT REQUIRED IMPORTED_TARGET mlt++-7)

add_library(isaac_core
    src/runtime_paths.cpp
    src/project.cpp
    src/captions.cpp
    src/text_style.cpp
    src/visual_transform.cpp
    src/visual_model.cpp
    src/visual_renderer.cpp
    src/text_renderer.cpp
    src/mlt_engine.cpp
    src/preview_cache.cpp
)
target_include_directories(isaac_core PUBLIC src)
target_link_libraries(isaac_core PUBLIC Qt6::Core Qt6::Gui PkgConfig::MLT)

add_library(isaac_ui
    src/recovery_store.cpp
    src/editor_jobs.cpp
    src/editor_controller.cpp
    src/composition_controller.cpp
    src/caption_controller.cpp
    src/preview.cpp
    src/preview_item.cpp
    src/quick_timeline.cpp
    src/media_cache.cpp
    src/qml_registration.cpp
)
target_include_directories(isaac_ui PUBLIC src)
target_link_libraries(isaac_ui PUBLIC isaac_core Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Widgets)

add_executable(IsaacVideoEditor src/main.cpp src/theme.cpp assets/assets.qrc qml/qml.qrc)
target_link_libraries(IsaacVideoEditor PRIVATE isaac_ui)
if(APPLE)
    target_sources(IsaacVideoEditor PRIVATE assets/IsaacVideoEditor.icns)
    set_source_files_properties(assets/IsaacVideoEditor.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(IsaacVideoEditor PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "IsaacVideoEditor"
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.isaacvideo.editor"
        MACOSX_BUNDLE_ICON_FILE "IsaacVideoEditor.icns"
    )
    # Finder displays the bundle folder date, which linking alone does not update.
    add_custom_command(TARGET IsaacVideoEditor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E touch "$<TARGET_BUNDLE_DIR:IsaacVideoEditor>"
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING)
find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

add_executable(runtime_paths_test tests/runtime_paths_test.cpp src/runtime_paths.cpp)
target_include_directories(runtime_paths_test PRIVATE src)
target_link_libraries(runtime_paths_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME runtime_paths_test COMMAND runtime_paths_test)

add_executable(project_test tests/project_test.cpp)
target_link_libraries(project_test PRIVATE isaac_core Qt6::Test)
add_test(NAME project_test COMMAND project_test)

add_executable(clip_list_test tests/clip_list_test.cpp src/clip_list.cpp)
target_include_directories(clip_list_test PRIVATE src)
target_link_libraries(clip_list_test PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME clip_list_test COMMAND clip_list_test)
set_tests_properties(clip_list_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(transport_test tests/transport_test.cpp
    src/main_window.cpp src/preview.cpp src/clip_list.cpp src/timeline_widget.cpp assets/assets.qrc)
target_include_directories(transport_test PRIVATE src)
target_link_libraries(transport_test PRIVATE isaac_core Qt6::Widgets Qt6::Gui Qt6::Test)
add_test(NAME transport_test COMMAND transport_test)
set_tests_properties(transport_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(theme_test tests/theme_test.cpp src/theme.cpp)
target_sources(theme_test PRIVATE assets/assets.qrc)
target_include_directories(theme_test PRIVATE src)
target_link_libraries(theme_test PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME theme_test COMMAND theme_test)
set_tests_properties(theme_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(timeline_widget_test tests/timeline_widget_test.cpp src/timeline_widget.cpp)
target_include_directories(timeline_widget_test PRIVATE src)
target_link_libraries(timeline_widget_test PRIVATE isaac_core Qt6::Widgets Qt6::Test)
add_test(NAME timeline_widget_test COMMAND timeline_widget_test)
set_tests_properties(timeline_widget_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rotation_engine_test tests/rotation_engine_test.cpp)
target_include_directories(rotation_engine_test PRIVATE src)
target_link_libraries(rotation_engine_test PRIVATE isaac_core Qt6::Gui Qt6::Test)
add_test(NAME rotation_engine_test COMMAND rotation_engine_test)

add_executable(media_info_test tests/media_info_test.cpp)
target_link_libraries(media_info_test PRIVATE isaac_core Qt6::Test)
add_test(NAME media_info_test COMMAND media_info_test)

add_executable(preview_cache_test tests/preview_cache_test.cpp)
target_link_libraries(preview_cache_test PRIVATE isaac_core Qt6::Test)
add_test(NAME preview_cache_test COMMAND preview_cache_test)

add_executable(preview_profile_test tests/preview_profile_test.cpp src/preview.cpp)
target_link_libraries(preview_profile_test PRIVATE isaac_core Qt6::Gui Qt6::Test)
add_test(NAME preview_profile_test COMMAND preview_profile_test)

foreach(test_name caption_controller_test caption_test source_browser_test editor_controller_test media_cache_test quick_timeline_test preview_async_test qml_shell_test text_overlay_test recovery_export_test)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE isaac_ui Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;SDL_AUDIODRIVER=dummy")
endforeach()
target_sources(qml_shell_test PRIVATE src/theme.cpp assets/assets.qrc)
target_sources(recovery_export_test PRIVATE assets/assets.qrc)

add_executable(interactions_integration_test tests/interactions_integration_test.cpp)
target_link_libraries(interactions_integration_test PRIVATE isaac_ui Qt6::Test)
add_test(NAME interactions_integration_test COMMAND interactions_integration_test)
set_tests_properties(interactions_integration_test PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;SDL_AUDIODRIVER=dummy")

add_executable(audio_transition_test tests/audio_transition_test.cpp)
target_link_libraries(audio_transition_test PRIVATE isaac_ui Qt6::Test)
add_test(NAME audio_transition_test COMMAND audio_transition_test)
set_tests_properties(audio_transition_test PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;SDL_AUDIODRIVER=dummy")

add_executable(composition_test tests/composition_test.cpp)
target_link_libraries(composition_test PRIVATE isaac_ui Qt6::Test)
add_test(NAME composition_test COMMAND composition_test)
add_executable(composition_controller_test tests/composition_controller_test.cpp)
target_link_libraries(composition_controller_test PRIVATE isaac_ui Qt6::Test)
add_test(NAME composition_controller_test COMMAND composition_controller_test)
set_tests_properties(composition_controller_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;SDL_AUDIODRIVER=dummy")
endif()
