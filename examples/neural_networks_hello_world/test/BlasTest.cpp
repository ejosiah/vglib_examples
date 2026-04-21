#include <gtest/gtest.h>
#include <vulkan_context.hpp>
#include <chrono>
#include <functional>

#include "cpu/functions.hpp"
#include "device/blas/blas.h"
#include "Profiler.hpp"

class BlasFixture : public ::testing::Test {
protected:
    struct Extent {
        uint32_t rows{};
        uint32_t cols{};
    };

    struct TestData {
        nda::matrix<float> expected;
        VulkanBuffer aBuffer;
        VulkanBuffer bBuffer;
        VulkanBuffer cBuffer;
        blas::matrix A;
        blas::matrix B;
        blas::matrix C;
    };

    struct TransposeTestData {
        nda::matrix<float> expected;
        VulkanBuffer aBuffer;
        VulkanBuffer bBuffer;
        blas::matrix A;
        blas::matrix B;
    };

    struct ElementwiseTestData {
        nda::matrix<float> expected;
        VulkanBuffer aBuffer;
        VulkanBuffer bBuffer;
        VulkanBuffer cBuffer;
        blas::matrix A;
        blas::matrix B;
        blas::matrix C;
    };

    std::unique_ptr<VulkanContext> context;

    void SetUp() override {
        spdlog::set_level(spdlog::level::debug);
        updateSearchPath();
        initVulkan();
        blas::init(context->device);
    }
    void TearDown() override {
        blas::shutdown();
    }

    void initVulkan() {
        ContextCreateInfo info{
            .settings =  {
                .queueFlags =  VK_QUEUE_COMPUTE_BIT,
            }
        };
        info.instanceExtAndLayers.extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        context = std::make_unique<VulkanContext>(info);
        context->init();
    }

    static void updateSearchPath() {
        std::filesystem::current_path("../examples");
        spdlog::info("working directory: {}", std::filesystem::current_path().string());
        FileManager::instance().addSearchPathFront(".");
        FileManager::instance().addSearchPathFront("../data");
        FileManager::instance().addSearchPathFront("neural_networks_hello_world/spv");
    }

    static std::string formatDuration(double totalSeconds) {
        const auto hours = static_cast<int>(totalSeconds / 3600.0);
        totalSeconds -= hours * 3600.0;
        const auto minutes = static_cast<int>(totalSeconds / 60.0);
        totalSeconds -= minutes * 60.0;
        return fmt::format("{:02}:{:02}:{:06.3f}", hours, minutes, totalSeconds);
    }

    TestData createDotProductTestData(Extent aExtent, Extent bExtent, bool identityA = true) {
        assert(aExtent.cols == bExtent.rows);

        nda::matrix<float> a{{aExtent.rows, aExtent.cols}, 0.0f};
        nda::matrix<float> b{{bExtent.rows, bExtent.cols}, 0.0f};

        nda::for_all_indices(a.shape(), [&](auto i, auto j) {
            if (identityA) {
                a(i, j) = i == j ? 1.0f : 0.0f;
            } else {
                a(i, j) = static_cast<float>((i * 19 + j * 23 + 3) % 89) / 89.0f;
            }
        });

        nda::for_all_indices(b.shape(), [&](auto i, auto j) {
            b(i, j) = static_cast<float>((i * 31 + j * 17) % 97) / 97.0f;
        });

        std::vector<float> aData;
        std::vector<float> bData;
        aData.reserve(aExtent.rows * aExtent.cols);
        bData.reserve(bExtent.rows * bExtent.cols);

        nda::for_all_indices(a.shape(), [&](auto i, auto j) {
            aData.push_back(a(i, j));
        });

        nda::for_all_indices(b.shape(), [&](auto i, auto j) {
            bData.push_back(b(i, j));
        });

        auto aBuffer = context->device.createCpuVisibleBuffer(aData.data(), BYTE_SIZE(aData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto bBuffer = context->device.createCpuVisibleBuffer(bData.data(), BYTE_SIZE(bData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        const auto resultRows = a.i().extent();
        const auto resultCols = b.j().extent();
        const auto resultSize = resultRows * resultCols;
        std::vector<float> cData(resultSize, 0.0f);
        auto cBuffer = context->device.createCpuVisibleBuffer(cData.data(), BYTE_SIZE(cData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        const auto start = std::chrono::steady_clock::now();
        const auto expected = dot(a, b);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto elapsedSeconds = std::chrono::duration<double>(elapsed).count();
        spdlog::info(
            "cpu reference dot(a, b) took {}",
            formatDuration(elapsedSeconds)
        );

        TestData data{
            .expected = std::move(expected),
            .aBuffer = std::move(aBuffer),
            .bBuffer = std::move(bBuffer),
            .cBuffer = std::move(cBuffer),
        };
        data.A = { data.aBuffer, {to<uint>(a.i().extent()), to<uint>(a.j().extent())} };
        data.B = { data.bBuffer, {to<uint>(b.i().extent()), to<uint>(b.j().extent())} };
        data.C = { data.cBuffer, {to<uint>(resultRows), to<uint>(resultCols)} };
        return data;
    }

    void runDotProductScenario(Extent aExtent, Extent bExtent) {
        auto data = createDotProductTestData(aExtent, bExtent, false);

        context->device.computeCommandPool().oneTimeCommand([&](auto commandBuffer) {
            blas::dot_product(commandBuffer, data.A, data.B, data.C);
        });

        ASSERT_EQ(data.C.shape.i, aExtent.rows);
        ASSERT_EQ(data.C.shape.j, bExtent.cols);

        const auto actual = data.cBuffer.span<float>(data.C.buffer.sizeAs<float>());
        nda::for_all_indices(data.expected.shape(), [&](auto i, auto j) {
            const auto index = i * data.expected.j().extent() + j;
            ASSERT_NEAR(actual[index], data.expected(i, j), 1E-3)
                << "mismatch at (" << i << ", " << j << ")";
        });
        data.cBuffer.unmap();
    }

    static std::vector<float> flatten(const nda::matrix<float>& x) {
        std::vector<float> data;
        data.reserve(x.i().extent() * x.j().extent());
        nda::for_all_indices(x.shape(), [&](auto i, auto j) {
            data.push_back(x(i, j));
        });
        return data;
    }

    ElementwiseTestData createUnaryElementwiseTestData(Extent extent, const std::function<nda::matrix<float>(nda::matrix<float>)>& expectedFn) {
        nda::matrix<float> a{{extent.rows, extent.cols}, 0.0f};
        nda::for_all_indices(a.shape(), [&](auto i, auto j) {
            const auto value = static_cast<int>((i * 17 + j * 29) % 41) - 20;
            a(i, j) = static_cast<float>(value) / 7.0f;
        });

        auto expected = expectedFn(a);
        auto aData = flatten(a);
        std::vector<float> cData(extent.rows * extent.cols, 0.0f);

        auto aBuffer = context->device.createCpuVisibleBuffer(aData.data(), BYTE_SIZE(aData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto cBuffer = context->device.createCpuVisibleBuffer(cData.data(), BYTE_SIZE(cData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        ElementwiseTestData data{
            .expected = std::move(expected),
            .aBuffer = std::move(aBuffer),
            .cBuffer = std::move(cBuffer),
        };
        data.A = { data.aBuffer, {extent.rows, extent.cols} };
        data.C = { data.cBuffer, {extent.rows, extent.cols} };
        return data;
    }

    ElementwiseTestData createBinaryElementwiseTestData(Extent extent, const std::function<nda::matrix<float>(const nda::matrix<float>&, const nda::matrix<float>&)>& expectedFn) {
        nda::matrix<float> a{{extent.rows, extent.cols}, 0.0f};
        nda::matrix<float> b{{extent.rows, extent.cols}, 0.0f};
        nda::for_all_indices(a.shape(), [&](auto i, auto j) {
            a(i, j) = static_cast<float>((i * 11 + j * 7) % 53) / 13.0f;
            b(i, j) = static_cast<float>((i * 5 + j * 19) % 47) / 17.0f;
        });

        auto expected = expectedFn(a, b);
        auto aData = flatten(a);
        auto bData = flatten(b);
        std::vector<float> cData(extent.rows * extent.cols, 0.0f);

        auto aBuffer = context->device.createCpuVisibleBuffer(aData.data(), BYTE_SIZE(aData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto bBuffer = context->device.createCpuVisibleBuffer(bData.data(), BYTE_SIZE(bData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto cBuffer = context->device.createCpuVisibleBuffer(cData.data(), BYTE_SIZE(cData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        ElementwiseTestData data{
            .expected = std::move(expected),
            .aBuffer = std::move(aBuffer),
            .bBuffer = std::move(bBuffer),
            .cBuffer = std::move(cBuffer),
        };
        data.A = { data.aBuffer, {extent.rows, extent.cols} };
        data.B = { data.bBuffer, {extent.rows, extent.cols} };
        data.C = { data.cBuffer, {extent.rows, extent.cols} };
        return data;
    }

    void assertElementwiseResult(ElementwiseTestData& data, float tolerance = 1E-5f) {
        ASSERT_EQ(data.C.shape.i, data.expected.i().extent());
        ASSERT_EQ(data.C.shape.j, data.expected.j().extent());

        const auto actual = data.cBuffer.span<float>(data.C.buffer.sizeAs<float>());
        nda::for_all_indices(data.expected.shape(), [&](auto i, auto j) {
            const auto index = i * data.expected.j().extent() + j;
            ASSERT_NEAR(actual[index], data.expected(i, j), tolerance)
                << "mismatch at (" << i << ", " << j << ")";
        });
        data.cBuffer.unmap();
    }

    TransposeTestData createTransposeTestData(Extent aExtent) {
        nda::matrix<float> a{{aExtent.rows, aExtent.cols}, 0.0f};
        nda::for_all_indices(a.shape(), [&](auto i, auto j) {
            a(i, j) = static_cast<float>((i * 37 + j * 13) % 101) / 101.0f;
        });

        std::vector<float> aData;
        aData.reserve(aExtent.rows * aExtent.cols);
        nda::for_all_indices(a.shape(), [&](auto i, auto j) {
            aData.push_back(a(i, j));
        });

        auto aBuffer = context->device.createCpuVisibleBuffer(aData.data(), BYTE_SIZE(aData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        const auto resultRows = a.j().extent();
        const auto resultCols = a.i().extent();
        std::vector<float> bData(resultRows * resultCols, 0.0f);
        auto bBuffer = context->device.createCpuVisibleBuffer(bData.data(), BYTE_SIZE(bData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        auto expected = transpose(a);

        TransposeTestData data{
            .expected = std::move(expected),
            .aBuffer = std::move(aBuffer),
            .bBuffer = std::move(bBuffer),
        };
        data.A = { data.aBuffer, {to<uint>(a.i().extent()), to<uint>(a.j().extent())} };
        data.B = { data.bBuffer, {to<uint>(resultRows), to<uint>(resultCols)} };
        return data;
    }
};

TEST_F(BlasFixture, dotProductSquareMatrix) {
    constexpr uint32_t workgroupSizeX = 1024;
    auto data = createDotProductTestData({workgroupSizeX, workgroupSizeX}, {workgroupSizeX, workgroupSizeX});

    context->device.computeCommandPool().oneTimeCommand([&](auto commandBuffer) {
        blas::dot_product(commandBuffer, data.A, data.B, data.C);
    });

    const auto actual = data.cBuffer.span<float>(data.C.buffer.sizeAs<float>());
    nda::for_all_indices(data.expected.shape(), [&](auto i, auto j) {
        const auto index = i * data.expected.j().extent() + j;
        if (std::abs(actual[index] - data.expected(i, j)) >= 1E-4) {
            throw std::runtime_error(fmt::format("{} != {} at [{}, {}]",actual[index], data.expected(i, j), i, j));
        }
        ASSERT_NEAR(actual[index], data.expected(i, j), 1E-4)
            << "mismatch at (" << i << ", " << j << ")";
    });
    data.cBuffer.unmap();
}

TEST_F(BlasFixture, dotProductSquareMatrixLessThanWorkGroup) {
    constexpr uint32_t workgroupSizeX = 600;
    auto data = createDotProductTestData({workgroupSizeX, workgroupSizeX}, {workgroupSizeX, workgroupSizeX});

    context->device.computeCommandPool().oneTimeCommand([&](auto commandBuffer) {
        blas::dot_product(commandBuffer, data.A, data.B, data.C);
    });


    const auto actual = data.cBuffer.span<float>(data.C.buffer.sizeAs<float>());
    nda::for_all_indices(data.expected.shape(), [&](auto i, auto j) {
        const auto index = i * data.expected.j().extent() + j;
        ASSERT_NEAR(actual[index], data.expected(i, j), 1E-4)
            << "mismatch at (" << i << ", " << j << ")";
    });
    data.cBuffer.unmap();
}

TEST_F(BlasFixture, dotProductNonSquareMatrices) {
    auto data = createDotProductTestData({1024, 512}, {512, 256});

    context->device.computeCommandPool().oneTimeCommand([&](auto commandBuffer) {
        blas::dot_product(commandBuffer, data.A, data.B, data.C);
    });

    ASSERT_EQ(data.C.shape.i, 1024);
    ASSERT_EQ(data.C.shape.j, 256);

    const auto actual = data.cBuffer.span<float>(data.C.buffer.sizeAs<float>());
    nda::for_all_indices(data.expected.shape(), [&](auto i, auto j) {
        const auto index = i * data.expected.j().extent() + j;
        ASSERT_NEAR(actual[index], data.expected(i, j), 1E-4)
            << "mismatch at (" << i << ", " << j << ")";
    });
    data.cBuffer.unmap();
}

TEST_F(BlasFixture, dotProductRowVectorTimesColumnVector) {
    runDotProductScenario({1, 777}, {777, 1});
}

TEST_F(BlasFixture, dotProductColumnVectorTimesRowVector) {
    runDotProductScenario({777, 1}, {1, 777});
}

TEST_F(BlasFixture, dotProductRowVectorTimesMatrix) {
    runDotProductScenario({1, 777}, {777, 193});
}

TEST_F(BlasFixture, dotProductMatrixTimesColumnVector) {
    runDotProductScenario({193, 777}, {777, 1});
}

TEST_F(BlasFixture, dotProductTallMatrixTimesWideMatrix) {
    runDotProductScenario({257, 129}, {129, 333});
}

TEST_F(BlasFixture, dotProductWideMatrixTimesTallMatrix) {
    runDotProductScenario({129, 333}, {333, 257});
}

TEST_F(BlasFixture, sigmoid) {
    auto data = createUnaryElementwiseTestData({37, 29}, [](auto x) {
        return ::sigmoid(std::move(x));
    });

    context->device.computeCommandPool().oneTimeCommand([&](auto commandBuffer) {
        blas::sigmoid(commandBuffer, data.A, data.C);
    });

    assertElementwiseResult(data);
}

TEST_F(BlasFixture, sigmoidPrime) {
    auto data = createUnaryElementwiseTestData({31, 43}, [](auto x) {
        return ::sigmoid_prime(std::move(x));
    });

    context->device.computeCommandPool().oneTimeCommand([&](auto commandBuffer) {
        blas::sigmoid_prime(commandBuffer, data.A, data.C);
    });

    assertElementwiseResult(data);
}

TEST_F(BlasFixture, costDerivative) {
    auto data = createBinaryElementwiseTestData({41, 23}, [](const auto& a, const auto& y) {
        return ::cost_derivative(a, y);
    });

    context->device.computeCommandPool().oneTimeCommand([&](auto commandBuffer) {
        blas::cost_derivative(commandBuffer, data.A, data.B, data.C);
    });

    assertElementwiseResult(data);
}

TEST_F(BlasFixture, add) {
    auto data = createBinaryElementwiseTestData({53, 17}, [](const auto& a, const auto& b) {
        return ::add(a, b);
    });

    context->device.computeCommandPool().oneTimeCommand([&](auto commandBuffer) {
        blas::add(commandBuffer, data.A, data.B, data.C);
    });

    assertElementwiseResult(data);
}

TEST_F(BlasFixture, transposeNonSquareMatrix) {
    auto data = createTransposeTestData({96, 128});

    context->device.computeCommandPool().oneTimeCommand([&](auto commandBuffer) {
        blas::transpose(commandBuffer, data.A, data.B);
    });

    ASSERT_EQ(data.B.shape.i, 128);
    ASSERT_EQ(data.B.shape.j, 96);

    const auto actual = data.bBuffer.span<float>(data.B.buffer.sizeAs<float>());
    nda::for_all_indices(data.expected.shape(), [&](auto i, auto j) {
        const auto index = i * data.expected.j().extent() + j;
        ASSERT_NEAR(actual[index], data.expected(i, j), 1E-4)
            << "transpose mismatch at (" << i << ", " << j << ")";
    });
    data.bBuffer.unmap();
}


TEST_F(BlasFixture, transposeSquareMatrix) {
    auto data = createTransposeTestData({1024, 1024});

    context->device.computeCommandPool().oneTimeCommand([&](auto commandBuffer) {
        blas::transpose(commandBuffer, data.A, data.B);
    });

    ASSERT_EQ(data.B.shape.i, 1024);
    ASSERT_EQ(data.B.shape.j, 1024);

    const auto actual = data.bBuffer.span<float>(data.B.buffer.sizeAs<float>());
    nda::for_all_indices(data.expected.shape(), [&](auto i, auto j) {
        const auto index = i * data.expected.j().extent() + j;
        ASSERT_NEAR(actual[index], data.expected(i, j), 1E-4)
            << "transpose mismatch at (" << i << ", " << j << ")";
    });
    data.bBuffer.unmap();
}
