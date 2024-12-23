cmake_minimum_required(VERSION 3.24)
project(openvdb)

set(CMAKE_CXX_STANDARD 20)

include_directories(${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb/include)
include_directories(ENV${VCPKG_PATH}/x64-windows/include)

find_library(OPENVDB_LIBRARY NAMES openvdb HINTS "${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb/lib" REQUIRED)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb/bin/openvdb.dll" DESTINATION "${CMAKE_BINARY_DIR}/bin/")

set(OPEN_VDB_LIBS
        ZLIB::ZLIB
        ENV${VCPKG_PATH}/installed/x64-windows/debug/lib/tbb12_debug.lib
        ENV${VCPKG_PATH}/installed/x64-windows/debug/lib/tbbmalloc_debug.lib
        ENV${VCPKG_PATH}/installed/x64-windows/debug/lib/tbbmalloc_proxy_debug.lib
        ENV${VCPKG_PATH}/installed/x64-windows/debug/lib/blosc.lib.lib
        Boost::boost
        ${OPENVDB_LIBRARY})

file(GLOB_RECURSE HEADER_FILES ${CMAKE_CURRENT_LIST_DIR}/include/*.* ENV${VCPKG_PATH}/x64-windows/include/*.*)

add_library(openvdb  ${HEADER_FILES} dumm.cpp)
target_link_libraries(openvdb PUBLIC ${OPEN_VDB_LIBS})
target_include_directories(openvdb PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb/include ENV${VCPKG_PATH}/x64-windows/include)