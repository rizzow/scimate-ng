cmake_minimum_required(VERSION 3.20)
project(scimate_ng VERSION 0.1.0 LANGUAGES CXX)

option(SCIMATE_BUILD_CLI "Build the resource inspection CLI" ON)
add_library(scimate_core core/src/resource_map.cpp)
target_include_directories(scimate_core PUBLIC core/include)
target_compile_features(scimate_core PUBLIC cxx_std_17)
set_target_properties(scimate_core PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(scimate_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(scimate_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SCIMATE_BUILD_CLI)
  add_executable(scimate apps/cli/main.cpp)
  target_link_libraries(scimate PRIVATE scimate_core)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(resource_map_tests tests/resource_map_tests.cpp)
  target_link_libraries(resource_map_tests PRIVATE scimate_core)
  add_test(NAME resource_map COMMAND resource_map_tests)
  if(SCIMATE_BUILD_CLI)
    add_test(NAME cli COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:scimate>
      -DFIXTURE_WRITER=$<TARGET_FILE:resource_map_tests>
      -DFIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-fixtures
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake)
  endif()
endif()
