#pragma once

#include "linear_system.hpp"

#include <fluid/FluidSolver2.hpp>
#include <vulkan_context.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace eular::spd_generator {
    struct config {
        std::filesystem::path output_file;
        size_t grid_size{16};
        double simulation_time{0.5};
        double time_step{1.0 / 120.0};
        double diffusion{0.01};
        int poisson_iterations{30};
    };

    struct velocity {
        double u{};
        double v{};
    };

    inline size_t cell_index(size_t x, size_t y, size_t grid_size) {
        return y * grid_size + x;
    }

    inline std::vector<double> divergence(const std::vector<velocity>& field, size_t grid_size) {
        std::vector<double> result(field.size());
        const auto inv_2dx = 0.5 * static_cast<double>(grid_size);

        for (size_t y = 0; y < grid_size; ++y) {
            for (size_t x = 0; x < grid_size; ++x) {
                const auto left = x == 0 ? velocity{} : field[cell_index(x - 1, y, grid_size)];
                const auto right = x + 1 == grid_size ? velocity{} : field[cell_index(x + 1, y, grid_size)];
                const auto down = y == 0 ? velocity{} : field[cell_index(x, y - 1, grid_size)];
                const auto up = y + 1 == grid_size ? velocity{} : field[cell_index(x, y + 1, grid_size)];

                result[cell_index(x, y, grid_size)] = (right.u - left.u + up.v - down.v) * inv_2dx;
            }
        }

        return result;
    }

    inline std::vector<double> pressure_matrix(size_t grid_size, double diffusion) {
        const auto n = grid_size * grid_size;
        std::vector<double> A(n * n);
        const auto scale = static_cast<double>((grid_size + 1) * (grid_size + 1));
        const auto diagonal = 4.0 * scale + diffusion;
        const auto off_diagonal = -scale;

        for (size_t y = 0; y < grid_size; ++y) {
            for (size_t x = 0; x < grid_size; ++x) {
                const auto row = cell_index(x, y, grid_size);
                A[row * n + row] = diagonal;

                if (x > 0) {
                    A[row * n + cell_index(x - 1, y, grid_size)] = off_diagonal;
                }

                if (x + 1 < grid_size) {
                    A[row * n + cell_index(x + 1, y, grid_size)] = off_diagonal;
                }

                if (y > 0) {
                    A[row * n + cell_index(x, y - 1, grid_size)] = off_diagonal;
                }

                if (y + 1 < grid_size) {
                    A[row * n + cell_index(x, y + 1, grid_size)] = off_diagonal;
                }
            }
        }

        return A;
    }

    inline VulkanDescriptorPool create_descriptor_pool(const VulkanDevice& device) {
        constexpr uint32_t max_sets = 1000;
        std::array<VkDescriptorPoolSize, 4> pool_sizes{
            {
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * max_sets},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * max_sets},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * max_sets},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * max_sets},
            }
        };

        return device.createDescriptorPool(max_sets, pool_sizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    }

    inline std::vector<float> read_scalar_texture(VulkanDevice& device,
                                                  const Texture& texture,
                                                  size_t grid_size) {
        const auto count = grid_size * grid_size;
        auto staging = device.createStagingBuffer(count * sizeof(float));

        device.firstActiveCommandPool().oneTimeCommand([&](VkCommandBuffer command_buffer) {
            VkImageMemoryBarrier2 to_transfer{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .image = texture.image.image,
                .subresourceRange = DEFAULT_SUB_RANGE,
            };

            VkDependencyInfo dependency{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &to_transfer,
            };
            vkCmdPipelineBarrier2(command_buffer, &dependency);

            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {
                static_cast<uint32_t>(grid_size),
                static_cast<uint32_t>(grid_size),
                1,
            };
            vkCmdCopyImageToBuffer(command_buffer,
                                   texture.image.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   staging.buffer,
                                   1,
                                   &region);

            VkImageMemoryBarrier2 to_general{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .image = texture.image.image,
                .subresourceRange = DEFAULT_SUB_RANGE,
            };

            dependency.pImageMemoryBarriers = &to_general;
            vkCmdPipelineBarrier2(command_buffer, &dependency);
        });

        const auto* values = static_cast<const float*>(staging.map());
        std::vector<float> result(values, values + count);
        staging.unmap();

        return result;
    }

    inline size_t step_count(const config& cfg) {
        return static_cast<size_t>(std::ceil(cfg.simulation_time / cfg.time_step));
    }

    inline std::vector<velocity> run_euler_solver(const config& cfg,
                                                  VulkanDevice& device,
                                                  VulkanDescriptorPool& descriptor_pool,
                                                  size_t steps) {
        constexpr auto two_pi = static_cast<float>(2.0 * std::numbers::pi);
        const auto grid = static_cast<float>(cfg.grid_size);
        auto solver =
            eular::FluidSolver::Builder{&device, &descriptor_pool}
                .gridSize({grid, grid})
                .generate([](auto x, auto y) {
                    return glm::vec2{glm::sin(two_pi * y), glm::sin(two_pi * x)};
                })
                .dt(static_cast<float>(cfg.time_step))
                .poissonIterations(cfg.poisson_iterations)
                .viscosity(0.0f)
                .enableWrapping()
                .useGaussSeidelSolver()
                .build();

        device.firstActiveCommandPool().oneTimeCommand([&](VkCommandBuffer command_buffer) {
            for (size_t i = 0; i < steps; ++i) {
                solver->runSimulation(command_buffer);
            }
        });

        auto& vector_field = solver->vectorField();
        const auto active_texture = steps % 2;
        const auto u = read_scalar_texture(device, vector_field.u[active_texture], cfg.grid_size);
        const auto v = read_scalar_texture(device, vector_field.v[active_texture], cfg.grid_size);

        std::vector<velocity> field(u.size());

        for (size_t i = 0; i < field.size(); ++i) {
            field[i] = {u[i], v[i]};
        }

        return field;
    }

    inline linear_system::system generate(const config& cfg, VulkanDevice& device, VulkanDescriptorPool& descriptor_pool) {
        if (cfg.grid_size < 2) {
            throw std::runtime_error{"grid_size must be at least 2"};
        }

        if (cfg.simulation_time < 0.0 || cfg.time_step <= 0.0) {
            throw std::runtime_error{"invalid simulation time or time step"};
        }

        if (cfg.poisson_iterations <= 0) {
            throw std::runtime_error{"poisson_iterations must be positive"};
        }

        const auto n = cfg.grid_size * cfg.grid_size;
        const auto steps = step_count(cfg);
        auto field = run_euler_solver(cfg, device, descriptor_pool, steps);

        auto A = pressure_matrix(cfg.grid_size, cfg.diffusion);
        auto b = divergence(field, cfg.grid_size);

        auto header = nlohmann::json{
            {"generator", "eular_vulkan_spd"},
            {"grid_size", cfg.grid_size},
            {"simulation_time", cfg.simulation_time},
            {"time_step", cfg.time_step},
            {"steps", steps},
            {"actual_simulation_time", static_cast<double>(steps) * cfg.time_step},
            {"diffusion", cfg.diffusion},
            {"poisson_iterations", cfg.poisson_iterations},
            {"source_solver", "eular::FluidSolver"},
            {"velocity_initial_condition", "sin(2*pi*y), sin(2*pi*x)"},
            {"matrix_storage", "coordinate"},
            {"index_base", linear_system::coordinate_index_base},
            {"matrix_kind", "dirichlet_pressure_poisson_spd"},
            {"non_zeros", linear_system::count_non_zero(A)},
        };

        return {std::move(header), n, n, std::move(A), std::move(b)};
    }

    inline linear_system::system generate_to_file(const config& cfg) {
        VkPhysicalDeviceVulkan12Features vulkan12_features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .scalarBlockLayout = VK_TRUE,
        };
        VkPhysicalDeviceVulkan13Features vulkan13_features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &vulkan12_features,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE,
            .maintenance4 = VK_TRUE,
        };

        ContextCreateInfo create_info{};
        create_info.applicationInfo.pApplicationName = "vglib linear system generator";
        create_info.settings.queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        create_info.deviceCreateNextChain = &vulkan13_features;

        VulkanContext context{create_info};
        context.init();

        auto descriptor_pool = create_descriptor_pool(context.device);
        auto system = generate(cfg, context.device, descriptor_pool);
        linear_system::save(cfg.output_file, system);

        return system;
    }
}
