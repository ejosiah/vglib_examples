# open vdb setup
set(CMAKE_TOOLCHAIN_FILE d:vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "vcpkg build system")

find_package(TBB CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(blosc  REQUIRED)
find_package(Boost REQUIRED)

include_directories(${CMAKE_CURRENT_LIST_DIR}/3rdParty/openvdb/include)

find_library(OPENVDB_LIBRARY NAMES openvdb HINTS "${CMAKE_CURRENT_LIST_DIR}/3rdParty/openvdb/lib" REQUIRED)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/3rdParty/openvdb/bin/openvdb.dll" DESTINATION "${CMAKE_BINARY_DIR}/bin/")

set(OPEN_VDB_LIBS
        ZLIB::ZLIB
        TBB::tbb
        TBB::tbbmalloc
        TBB::tbbmalloc_proxy
        blosc_shared
        Boost::boost
        ${OPENVDB_LIBRARY})

# open vdb setup end

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
target_link_libraries(open_vdb_viewer VulkanBase ImGuiPlugin ${OPEN_VDB_LIBS})

if (OpenCL_FOUND)
    include_directories("${OpenCL_INCLUDE_DIRS}")
    add_definitions( -DOPENCL_FOUND=1 )
    target_link_libraries(open_vdb_viewer ${OpenCL_LIBRARIES})
endif()