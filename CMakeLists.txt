cmake_minimum_required(VERSION 3.20)
project(scimate_ng VERSION 0.1.0 LANGUAGES CXX)

option(SCIMATE_BUILD_DESKTOP "Build the Qt project-management GUI" ON)
option(SCIMATE_BUILD_CLI "Build the resource inspection CLI" ON)
option(SCIMATE_BUILD_APPLICATION "Build filesystem import/export workflows" ${SCIMATE_BUILD_CLI})
add_library(scimate_core core/src/resource_map.cpp core/src/codecs.cpp core/src/resource_game.cpp core/src/script.cpp core/src/view.cpp core/src/picture.cpp core/src/assets.cpp core/src/json.cpp core/src/structured_resource.cpp core/src/sound.cpp core/src/explain.cpp core/src/sci1.cpp core/src/sci1_migration.cpp core/src/sci11_script.cpp)
target_sources(scimate_core PRIVATE core/src/rooms.cpp)
target_include_directories(scimate_core PUBLIC core/include)
target_sources(scimate_core PRIVATE core/src/explanation_output.cpp core/src/diagnostics.cpp
  core/src/structured_fields.cpp core/src/structured_script.cpp
  core/src/structured_vocabulary.cpp core/src/structured_patches.cpp
  core/src/structured_edit.cpp core/src/structured_schema.cpp core/src/migration_report.cpp)
find_package(Threads REQUIRED)
target_link_libraries(scimate_core PUBLIC Threads::Threads)
target_compile_features(scimate_core PUBLIC cxx_std_17)
set_target_properties(scimate_core PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(scimate_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(scimate_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SCIMATE_BUILD_APPLICATION OR SCIMATE_BUILD_CLI OR SCIMATE_BUILD_DESKTOP)
  find_package(PNG 1.6 REQUIRED)
  add_library(scimate_application application/src/game_io.cpp application/src/view_io.cpp application/src/picture_io.cpp application/src/asset_io.cpp application/src/project.cpp)
  target_sources(scimate_application PRIVATE application/src/sci1_io.cpp application/src/output.cpp)
  target_include_directories(scimate_application PUBLIC application/include)
  target_link_libraries(scimate_application PUBLIC scimate_core)
  target_link_libraries(scimate_application PRIVATE PNG::PNG)
  set_target_properties(scimate_application PROPERTIES CXX_EXTENSIONS OFF)
endif()

if(SCIMATE_BUILD_CLI)
  add_executable(scimate apps/cli/main.cpp)
  target_link_libraries(scimate PRIVATE scimate_application)
endif()

if(SCIMATE_BUILD_DESKTOP)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Concurrent)
  add_library(scimate_desktop_ui apps/desktop/main_window.cpp apps/desktop/explanation_tree.cpp apps/desktop/preferences.cpp apps/desktop/game_info.cpp)
  target_sources(scimate_desktop_ui PRIVATE apps/desktop/main_window_ui.cpp
    apps/desktop/main_window_operations.cpp apps/desktop/main_window_preview.cpp)
  target_include_directories(scimate_desktop_ui PUBLIC apps/desktop)
  target_sources(scimate_desktop_ui PRIVATE apps/desktop/font_preview.cpp)
  target_link_libraries(scimate_desktop_ui PUBLIC scimate_application Qt6::Widgets Qt6::Concurrent)
  add_executable(scimate-desktop MACOSX_BUNDLE WIN32 apps/desktop/main.cpp)
  target_link_libraries(scimate-desktop PRIVATE scimate_desktop_ui)
  qt_add_resources(scimate-desktop desktop_icons
    PREFIX "/"
    BASE "apps/desktop/resources"
    FILES apps/desktop/resources/scimate.png)
  if(APPLE)
    target_sources(scimate-desktop PRIVATE apps/desktop/resources/scimate.icns)
    set_source_files_properties(apps/desktop/resources/scimate.icns PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(scimate-desktop PROPERTIES MACOSX_BUNDLE_ICON_FILE scimate.icns)
  endif()
  set_target_properties(scimate-desktop PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "SCIMate NG"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.scimate.desktop")
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(rooms_tests tests/rooms_tests.cpp)
  target_link_libraries(rooms_tests PRIVATE scimate_core)
  add_test(NAME rooms COMMAND rooms_tests)
  add_executable(resource_map_tests tests/resource_map_tests.cpp)
  target_link_libraries(resource_map_tests PRIVATE scimate_core)
  add_test(NAME resource_map COMMAND resource_map_tests)
  add_executable(resource_game_tests tests/resource_game_tests.cpp)
  target_link_libraries(resource_game_tests PRIVATE scimate_core)
  add_test(NAME resource_game COMMAND resource_game_tests)
  add_executable(sci11_script_tests tests/sci11_script_tests.cpp)
  target_link_libraries(sci11_script_tests PRIVATE scimate_core)
  add_test(NAME sci11_script COMMAND sci11_script_tests)
  add_executable(sci1_tests tests/sci1_tests.cpp)
  target_link_libraries(sci1_tests PRIVATE scimate_core)
  add_test(NAME sci1 COMMAND sci1_tests)
  add_executable(explain_tests tests/explain_tests.cpp)
  target_link_libraries(explain_tests PRIVATE scimate_core)
  add_test(NAME explain COMMAND explain_tests)
  add_executable(script_tests tests/script_tests.cpp)
  target_link_libraries(script_tests PRIVATE scimate_core)
  add_test(NAME scripts COMMAND script_tests)
  add_executable(assets_tests tests/assets_tests.cpp)
  target_link_libraries(assets_tests PRIVATE scimate_core)
  add_test(NAME assets COMMAND assets_tests)
  add_executable(view_tests tests/view_tests.cpp)
  target_link_libraries(view_tests PRIVATE scimate_core)
  add_test(NAME views COMMAND view_tests)
  if(SCIMATE_BUILD_DESKTOP)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(desktop_tests tests/desktop_tests.cpp)
    set_target_properties(desktop_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(desktop_tests PRIVATE scimate_desktop_ui Qt6::Test)
    add_test(NAME desktop COMMAND desktop_tests)
    set_tests_properties(desktop PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
  if(TARGET scimate_application)
    add_executable(output_tests tests/output_tests.cpp)
    target_link_libraries(output_tests PRIVATE scimate_application)
    add_test(NAME output COMMAND output_tests ${CMAKE_CURRENT_BINARY_DIR}/output-fixtures)
    add_executable(project_tests tests/project_tests.cpp)
    target_link_libraries(project_tests PRIVATE scimate_application)
    add_test(NAME projects COMMAND project_tests ${CMAKE_CURRENT_BINARY_DIR}/project-fixtures)
    add_executable(asset_io_tests tests/asset_io_tests.cpp)
    target_link_libraries(asset_io_tests PRIVATE scimate_application)
    add_test(NAME asset_io COMMAND asset_io_tests ${CMAKE_CURRENT_BINARY_DIR}/asset-fixtures)
    add_executable(picture_tests tests/picture_tests.cpp)
    target_link_libraries(picture_tests PRIVATE scimate_application)
    add_test(NAME pictures COMMAND picture_tests ${CMAKE_CURRENT_BINARY_DIR}/picture-fixtures)
    add_executable(game_io_tests tests/game_io_tests.cpp)
    target_link_libraries(game_io_tests PRIVATE scimate_application)
    add_test(NAME game_io COMMAND game_io_tests ${CMAKE_CURRENT_BINARY_DIR}/io-fixtures)
    add_executable(view_io_tests tests/view_io_tests.cpp)
    target_link_libraries(view_io_tests PRIVATE scimate_application PNG::PNG)
    add_test(NAME view_io COMMAND view_io_tests ${CMAKE_CURRENT_BINARY_DIR}/view-fixtures)
  endif()
  if(SCIMATE_BUILD_CLI)
    add_test(NAME explain_cli COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:scimate>
      -DFIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/explain-cli-fixtures
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/explain_cli.cmake)
    add_test(NAME cli COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:scimate>
      -DFIXTURE_WRITER=$<TARGET_FILE:resource_map_tests>
      -DFIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-fixtures
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake)
    add_test(NAME pipeline_cli COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:scimate>
      -DFIXTURE_WRITER=$<TARGET_FILE:game_io_tests>
      -DFIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/pipeline-fixtures
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/pipeline_cli.cmake)
    add_test(NAME asset_cli COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:scimate>
      -DFIXTURE_WRITER=$<TARGET_FILE:asset_io_tests>
      -DFIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/asset-cli-fixtures
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/asset_cli.cmake)
    add_test(NAME picture_cli COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:scimate>
      -DFIXTURE_WRITER=$<TARGET_FILE:picture_tests>
      -DFIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/picture-cli-fixtures
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/picture_cli.cmake)
    add_test(NAME view_cli COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:scimate>
      -DFIXTURE_WRITER=$<TARGET_FILE:view_io_tests>
      -DFIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/view-cli-fixtures
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/view_cli.cmake)
  endif()
endif()
