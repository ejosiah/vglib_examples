cmake_minimum_required(VERSION 3.24)
project(vglib_examples)

set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/dependencies/vglib.github.io/cmake ${CMAKE_MODULE_PATH})

find_package(Vulkan)

set(LIB_DEPENDENCIES VulkanBase Vulkan::Vulkan ${CONAN_LIBS})

add_definitions(
        -DGLM_FORCE_RADIANS
        -DGLFW_INCLUDE_VULKAN
        -DGLM_FORCE_SWIZZLE
        -DSPDLOG_FMT_EXTERNAL
)

add_subdirectory(dependencies)
add_subdirectory(examples)
