
include_directories(include)

find_package(OPENXR REQUIRED)

file(GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_LIST_DIR}/source/*.cpp)
file(GLOB_RECURSE HPP_FILES ${CMAKE_CURRENT_LIST_DIR}/include/*.*)

add_executable(VulkanCube ${CPP_FILES} ${HPP_FILES})
target_link_libraries(VulkanCube VulkanBase Vulkan::Vulkan ${CONAN_LIBS} OpenXR::openxr_loader)