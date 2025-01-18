file(GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_LIST_DIR}/*.h ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

message(STATUS "CPP FILES ${CPP_FILES}")


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

add_executable(Playground EXCLUDE_FROM_ALL ${CPP_FILES})
#target_link_libraries(Playground VulkanBase ${LIBRARIES_DEBUG} ${OPEN_VDB_LIBS})
target_link_libraries(Playground VulkanBase ${LIBRARIES_DEBUG} common)

