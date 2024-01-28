file(GLOB_RECURSE HEADER_FILES ${CMAKE_CURRENT_LIST_DIR}/*.hpp)
file(GLOB_RECURSE SOURCE_FILES ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

set(OPEN_VDB_PATH "C:/Program Files/OpenVDB/lib/cmake/OpenVDB")

set(CMAKE_MODULE_PATH ${OPEN_VDB_PATH} ${CMAKE_MODULE_PATH})

find_package(OpenVDB REQUIRED)


add_library(common ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(common PRIVATE VulkanBase)