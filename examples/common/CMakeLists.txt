# open vdb setup
include_directories($ENV{VCPKG_PATH}/installed/x64-windows/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb/include)

find_library(OPENVDB_LIBRARY NAMES openvdb HINTS "${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb/lib" REQUIRED)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb/bin/openvdb.dll" DESTINATION "${CMAKE_BINARY_DIR}/bin/")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb/bin/tbb12_debug.dll" DESTINATION "${CMAKE_BINARY_DIR}/bin/")

set(OPEN_VDB_LIBS
        $ENV{VCPKG_PATH}/installed/x64-windows/debug/lib/tbb12_debug.lib
        $ENV{VCPKG_PATH}/installed/x64-windows/debug/lib/tbbmalloc_debug.lib
        $ENV{VCPKG_PATH}/installed/x64-windows/debug/lib/tbbmalloc_proxy_debug.lib
        $ENV{VCPKG_PATH}/installed/x64-windows/debug/lib/blosc.lib
        ${OPENVDB_LIBRARY})

file(GLOB_RECURSE HEADER_FILES ${CMAKE_CURRENT_LIST_DIR}/*.hpp)
file(GLOB_RECURSE SOURCE_FILES ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

set(COMMON_GLSL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/shaders")
set(COMMON_SPV_DIR "${CMAKE_CURRENT_LIST_DIR}/spv")
compile_glsl_directory(SRC_DIR "${COMMON_GLSL_SOURCE_DIR}" OUT_DIR "${COMMON_SPV_DIR}" INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/data/shaders")

add_library(common ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(common PUBLIC VulkanBase ${OPEN_VDB_LIBS} ${CONAN_LIBS})