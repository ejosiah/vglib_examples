#include <gtest/gtest.h>
#include <filesystem>

int main(int argc, char** argv) {
    std::filesystem::current_path("../../../");
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}