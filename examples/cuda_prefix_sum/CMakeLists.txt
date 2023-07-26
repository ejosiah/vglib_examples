include_directories(${CMAKE_CURRENT_LIST_DIR})


file(GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_LIST_DIR}/*.*)

add_executable(cuda_prefix_sum  ${CPP_FILES})
target_link_libraries(cuda_prefix_sum)

set_target_properties(cuda_prefix_sum PROPERTIES CUDA_ARCHITECTURES "35;50;72")
