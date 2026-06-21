# open vdb setup
set(OPENVDB_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../dependencies/openvdb")

find_library(OPENVDB_LIBRARY NAMES openvdb HINTS "${OPENVDB_ROOT}/lib" REQUIRED)

set(OPENVDB_RUNTIME_DLLS
        "${OPENVDB_ROOT}/bin/openvdb.dll"
        "${OPENVDB_ROOT}/bin/tbb12_debug.dll"
        "${OPENVDB_ROOT}/bin/blosc.dll"
        "${OPENVDB_ROOT}/bin/zlibd1.dll"
        "${OPENVDB_ROOT}/bin/zstd.dll"
        "${OPENVDB_ROOT}/bin/lz4d.dll")

foreach (OPENVDB_RUNTIME_DLL IN LISTS OPENVDB_RUNTIME_DLLS)
    file(INSTALL "${OPENVDB_RUNTIME_DLL}" DESTINATION "${CMAKE_BINARY_DIR}/bin/")
endforeach ()

set(OPENVDB_RUNTIME_DLLS ${OPENVDB_RUNTIME_DLLS} PARENT_SCOPE)

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
target_include_directories(common PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
        $ENV{VCPKG_PATH}/installed/x64-windows/include
        ${OPENVDB_ROOT}/include)
target_link_libraries(common PUBLIC VulkanBase ${OPEN_VDB_LIBS} ${CONAN_LIBS})

add_executable(linear_system_generator
        ${CMAKE_CURRENT_LIST_DIR}/tools/linear_system_generator.cpp)
target_include_directories(linear_system_generator PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(linear_system_generator PRIVATE VulkanBase nlohmann_json::nlohmann_json)
