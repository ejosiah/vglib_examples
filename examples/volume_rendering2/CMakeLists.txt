if (NOT NO_OPENCL)
    find_package(OpenCL QUIET)
endif()

set(OPEN_VDB_SOURCE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb)

file(INSTALL "${OPEN_VDB_SOURCE_PATH}/bin/openvdb.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/Debug")
file(INSTALL "${OPEN_VDB_SOURCE_PATH}/bin/tbb12_debug.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/Debug")
file(INSTALL "${OPEN_VDB_SOURCE_PATH}/bin/blosc.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/Debug")
file(INSTALL "${OPEN_VDB_SOURCE_PATH}/bin/zlibd1.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/Debug")
file(INSTALL "${OPEN_VDB_SOURCE_PATH}/bin/zstd.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/Debug")
file(INSTALL "${OPEN_VDB_SOURCE_PATH}/bin/lz4d.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/Debug")


include_directories($ENV{VCPKG_PATH}/installed/x64-windows/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
include_directories(${OPEN_VDB_SOURCE_PATH}/include)

include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

file(GLOB_RECURSE HPP_FILES ${CMAKE_CURRENT_LIST_DIR}/include/*.*)
file(GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_LIST_DIR}/source/*.cpp)

set(GLSL_SOURCE_DIR "shaders")
set(SPV_DIR "${CMAKE_CURRENT_LIST_DIR}/spv")
compile_glsl_directory(SRC_DIR "${GLSL_SOURCE_DIR}" OUT_DIR "${SPV_DIR}" INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/data/shaders")

add_executable(volume_rendering2 ${CPP_FILES} ${HPP_FILES})
target_link_libraries(volume_rendering2 VulkanBase ImGuiPlugin common)

if (OpenCL_FOUND)
    include_directories("${OpenCL_INCLUDE_DIRS}")
    add_definitions( -DOPENCL_FOUND=1 )
    target_link_libraries(volume_rendering2 ${OpenCL_LIBRARIES})
endif()