file(GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_LIST_DIR}/gpu_info.cu)


add_executable(cuda_eval  ${CPP_FILES})
target_link_libraries(cuda_eval)

set_target_properties(cuda_eval PROPERTIES CUDA_ARCHITECTURES "35;50;72")
