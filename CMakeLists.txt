cmake_minimum_required(VERSION 3.20)
project(scimate_ng VERSION 0.1.0 LANGUAGES CXX)

option(SCIMATE_BUILD_CLI "Build the resource inspection CLI" ON)
option(SCIMATE_BUILD_APPLICATION "Build filesystem import/export workflows" ${SCIMATE_BUILD_CLI})
add_library(scimate_core core/src/resource_map.cpp core/src/codecs.cpp core/src/resource_game.cpp core/src/script.cpp core/src/view.cpp)
target_include_directories(scimate_core PUBLIC core/include)
target_compile_features(scimate_core PUBLIC cxx_std_17)
set_target_properties(scimate_core PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(scimate_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(scimate_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SCIMATE_BUILD_APPLICATION OR SCIMATE_BUILD_CLI)
  find_package(PNG 1.6 REQUIRED)
  add_library(scimate_application application/src/game_io.cpp application/src/view_io.cpp)
  target_include_directories(scimate_application PUBLIC application/include)
  target_link_libraries(scimate_application PUBLIC scimate_core)
  target_link_libraries(scimate_application PRIVATE PNG::PNG)
  set_target_properties(scimate_application PROPERTIES CXX_EXTENSIONS OFF)
endif()

if(SCIMATE_BUILD_CLI)
  add_executable(scimate apps/cli/main.cpp)
  target_link_libraries(scimate PRIVATE scimate_application)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(resource_map_tests tests/resource_map_tests.cpp)
  target_link_libraries(resource_map_tests PRIVATE scimate_core)
  add_test(NAME resource_map COMMAND resource_map_tests)
  add_executable(resource_game_tests tests/resource_game_tests.cpp)
  target_link_libraries(resource_game_tests PRIVATE scimate_core)
  add_test(NAME resource_game COMMAND resource_game_tests)
  add_executable(script_tests tests/script_tests.cpp)
  target_link_libraries(script_tests PRIVATE scimate_core)
  add_test(NAME scripts COMMAND script_tests)
  add_executable(view_tests tests/view_tests.cpp)
  target_link_libraries(view_tests PRIVATE scimate_core)
  add_test(NAME views COMMAND view_tests)
  if(TARGET scimate_application)
    add_executable(game_io_tests tests/game_io_tests.cpp)
    target_link_libraries(game_io_tests PRIVATE scimate_application)
    add_test(NAME game_io COMMAND game_io_tests ${CMAKE_CURRENT_BINARY_DIR}/io-fixtures)
    add_executable(view_io_tests tests/view_io_tests.cpp)
    target_link_libraries(view_io_tests PRIVATE scimate_application PNG::PNG)
    add_test(NAME view_io COMMAND view_io_tests ${CMAKE_CURRENT_BINARY_DIR}/view-fixtures)
  endif()
  if(SCIMATE_BUILD_CLI)
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
    add_test(NAME view_cli COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:scimate>
      -DFIXTURE_WRITER=$<TARGET_FILE:view_io_tests>
      -DFIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/view-cli-fixtures
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/view_cli.cmake)
  endif()
endif()
