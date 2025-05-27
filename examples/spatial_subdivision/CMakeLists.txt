file(GLOB_RECURSE HEADER_FILES ${CMAKE_CURRENT_LIST_DIR}/*.hpp)
file(GLOB_RECURSE SOURCE_FILES ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

set(COMMON_GLSL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/shaders")
set(COMMON_SPV_DIR "${CMAKE_CURRENT_LIST_DIR}/spv")
compile_glsl_directory(SRC_DIR "${COMMON_GLSL_SOURCE_DIR}" OUT_DIR "${COMMON_SPV_DIR}" INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/data/shaders")

add_library(spatial_subdivision ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(spatial_subdivision PUBLIC VulkanBase ${CONAN_LIBS})