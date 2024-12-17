cmake_minimum_required(VERSION 3.24)
project(vglib_examples)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/dependencies/vglib.github.io/cmake ${CMAKE_MODULE_PATH})

find_package(Vulkan)
find_package(assimp)
find_package(glm)
find_package(glfw3)
find_package(spdlog)
find_package(freetype)
find_package(imgui)
find_package(Boost)
find_package(GTest)
find_package(argparse)
find_package(Bullet)
find_package(EnTT)
find_package(meshoptimizer)
find_package(OpenEXR)
find_package(Taskflow)
find_package(ZLIB)
find_package(TinyGLTF)
find_package(stb)

set(CONAN_LIBS   assimp::assimp glm::glm glfw spdlog::spdlog Freetype::Freetype imgui::imgui boost::boost gtest::gtest
                    argparse::argparse Bullet::Bullet EnTT::EnTT meshoptimizer::meshoptimizer openexr::openexr
                    Taskflow::Taskflow ZLIB::ZLIB TinyGLTF::TinyGLTF stb::stb)

set(LIB_DEPENDENCIES VulkanBase Vulkan::Vulkan ${CONAN_LIBS})

add_definitions(
        -DGLM_FORCE_RADIANS
        -DGLFW_INCLUDE_VULKAN
        -DGLM_FORCE_SWIZZLE
        -DSPDLOG_FMT_EXTERNAL
)

add_subdirectory(dependencies)
add_subdirectory(examples)
