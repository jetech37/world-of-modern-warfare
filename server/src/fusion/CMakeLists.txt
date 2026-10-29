cmake_minimum_required(VERSION 3.12)
if(NOT PROJECT_NAME)
  project(fusion CXX)
endif()

# Azeroth Warfare: the fusion rules, a standalone library the game links and its unit tests.

file(GLOB fusion_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(fusion STATIC ${fusion_SRCS})
target_include_directories(fusion PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fusion PUBLIC cxx_std_17)
set_target_properties(fusion PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(BUILD_FUSION_TESTS "Build the fusion rules' unit tests" ON)
if(BUILD_FUSION_TESTS)
  file(GLOB fusion_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
  add_executable(fusion_tests ${fusion_TESTS})
  target_link_libraries(fusion_tests PRIVATE fusion)
  target_compile_definitions(fusion_tests PRIVATE
    FUSION_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors"
    FUSION_SEED_WEAPONS="${CMAKE_CURRENT_SOURCE_DIR}/../../contrib/fusion/weapons.json")
  enable_testing()
  add_test(NAME fusion_tests COMMAND fusion_tests)
endif()
