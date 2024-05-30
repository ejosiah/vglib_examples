# open vdb setup
set(CMAKE_TOOLCHAIN_FILE d:vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "vcpkg build system")

find_package(TBB CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(blosc  REQUIRED)
find_package(Boost REQUIRED)

include_directories(${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb/include)

find_library(OPENVDB_LIBRARY NAMES openvdb HINTS "${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb/lib" REQUIRED)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb/bin/openvdb.dll" DESTINATION "${CMAKE_BINARY_DIR}/bin/")

set(OPEN_VDB_LIBS
        ZLIB::ZLIB
        TBB::tbb
        TBB::tbbmalloc
        TBB::tbbmalloc_proxy
        blosc_shared
        Boost::boost
        ${OPENVDB_LIBRARY})

file(GLOB_RECURSE HEADER_FILES ${CMAKE_CURRENT_LIST_DIR}/*.hpp)
file(GLOB_RECURSE SOURCE_FILES ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

set(COMMON_GLSL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/shaders")
set(COMMON_SPV_DIR "${CMAKE_CURRENT_LIST_DIR}/spv")
compile_glsl_directory(SRC_DIR "${COMMON_GLSL_SOURCE_DIR}" OUT_DIR "${COMMON_SPV_DIR}" INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/data/shaders")

add_library(common ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(common PUBLIC VulkanBase ${OPEN_VDB_LIBS})