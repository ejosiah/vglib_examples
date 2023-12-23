file(GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_LIST_DIR}/*.h ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

message(STATUS "CPP FILES ${CPP_FILES}")

add_executable(Playground EXCLUDE_FROM_ALL ${CPP_FILES})
#target_link_libraries(Playground VulkanBase ${LIBRARIES_DEBUG} ${OPEN_VDB_LIBS})
target_link_libraries(Playground VulkanBase ${LIBRARIES_DEBUG})

