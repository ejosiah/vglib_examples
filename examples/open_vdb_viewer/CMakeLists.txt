if (NOT NO_OPENCL)
    find_package(OpenCL QUIET)
endif()

include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

file(GLOB_RECURSE HPP_FILES ${CMAKE_CURRENT_LIST_DIR}/include/*.*)
file(GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_LIST_DIR}/source/*.cpp)

set(GLSL_SOURCE_DIR "shaders")
set(SPV_DIR "${CMAKE_CURRENT_LIST_DIR}/spv")
compile_glsl_directory(SRC_DIR "${GLSL_SOURCE_DIR}" OUT_DIR "${SPV_DIR}" INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/data/shaders")

add_executable(open_vdb_viewer ${CPP_FILES} ${HPP_FILES})
target_link_libraries(open_vdb_viewer VulkanBase ImGuiPlugin common)
add_custom_command(TARGET open_vdb_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENVDB_RUNTIME_DLLS} "$<TARGET_FILE_DIR:open_vdb_viewer>"
        COMMAND_EXPAND_LISTS)

if (OpenCL_FOUND)
    include_directories("${OpenCL_INCLUDE_DIRS}")
    add_definitions( -DOPENCL_FOUND=1 )
    target_link_libraries(open_vdb_viewer ${OpenCL_LIBRARIES})
endif()
