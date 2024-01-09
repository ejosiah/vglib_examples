file(GLOB_RECURSE HEADER_FILES ${CMAKE_CURRENT_LIST_DIR}/*.hpp)
file(GLOB_RECURSE SOURCE_FILES ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

add_library(common ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(common PRIVATE VulkanBase)