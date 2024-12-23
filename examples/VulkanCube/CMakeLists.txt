
include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
include_directories(ENV${VCPKG_PATH}/x64-windows/include)

file(GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_LIST_DIR}/source/*.cpp)
file(GLOB_RECURSE HPP_FILES ${CMAKE_CURRENT_LIST_DIR}/include/*.*)

add_executable(VulkanCube ${CPP_FILES} ${HPP_FILES})
target_link_libraries(VulkanCube PRIVATE VulkanBase Vulkan::Vulkan ${CONAN_LIBS})

message(STATUS "tbb include path $ENV{VCPKG_PATH}/x64-windows/include")