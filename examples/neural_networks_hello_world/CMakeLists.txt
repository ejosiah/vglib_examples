find_package(GTest)

include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

file(GLOB_RECURSE HPP_FILES ${CMAKE_CURRENT_LIST_DIR}/include/*.*)
file(GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_LIST_DIR}/source/*.cpp)
file(GLOB_RECURSE TEST_FILES ${CMAKE_CURRENT_LIST_DIR}/test/*.*)
set(TEST_CPP_FILES ${CPP_FILES})
list(FILTER TEST_CPP_FILES EXCLUDE REGEX ".*/NeuralNetworksDemo\\.cpp$")

set(GLSL_SOURCE_DIR "shaders")
set(SPV_DIR "${CMAKE_CURRENT_LIST_DIR}/spv")
compile_glsl_directory(SRC_DIR "${GLSL_SOURCE_DIR}" OUT_DIR "${SPV_DIR}" INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/data/shaders")

add_executable(neural_networks_hello_world ${CPP_FILES} ${HPP_FILES})
target_link_libraries(neural_networks_hello_world VulkanBase ImGuiPlugin common)

enable_testing()
add_executable(neural_networks_hello_test ${TEST_CPP_FILES} ${HPP_FILES} ${TEST_FILES})
target_link_libraries(neural_networks_hello_test PRIVATE VulkanBase ImGuiPlugin common Vulkan::Vulkan  gtest::gtest)

include(GoogleTest)
gtest_discover_tests(neural_networks_hello_test)
