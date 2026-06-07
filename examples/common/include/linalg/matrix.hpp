#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <VulkanBuffer.h>

#include "VulkanDevice.h"

namespace linalg {
    struct cpu_backend {};

    struct vulkan_backend {};

    template<typename Backend, typename T>
    struct buffer_type;

    template<typename T>
    struct buffer_type<cpu_backend, T> {
        using type = std::vector<T>;
    };

    template<typename T>
    struct buffer_type<vulkan_backend, T> {
        using type = VulkanBuffer;
    };

    template<typename Backend, typename T>
    using buffer_type_t = typename buffer_type<Backend, T>::type;

    enum class matrix_storage { dense, sparse };

    enum class matrix_layout_kind { row_major, column_major, csr, csc };

    enum class backend_kind { cpu, gpu };

    struct row_major_tag {};

    struct column_major_tag {};

    struct csr_tag {};

    struct csc_tag {};

    template<typename T, size_t Rows, size_t Cols, typename LayoutTag, typename Backend = cpu_backend>
    struct matrix;

    /**
     * Dense matrix, row major
     * @tparam T
     * @tparam Rows
     * @tparam Cols
     */
    template<typename T, size_t Rows, size_t Cols, typename Backend>
    struct matrix<T, Rows, Cols, row_major_tag, Backend> {
        using value_type = T;
        using layout = row_major_tag;
        using backend = Backend;

        static constexpr size_t rows = Rows;
        static constexpr size_t cols = Cols;

        static constexpr matrix_storage storage = matrix_storage::dense;
        static constexpr matrix_layout_kind matrix_layout = matrix_layout_kind::row_major;

        buffer_type_t<Backend, T> data;

        T& operator()(size_t r, size_t c)
        requires std::is_same_v<Backend, cpu_backend> {
            return data[r * Cols + c];
        }

        const T& operator()(size_t r, size_t c) const
        requires std::is_same_v<Backend, cpu_backend> {
            return data[r * Cols + c];
        }

    };

    /**
     * Dense matrix column major
     * @tparam T
     * @tparam Rows
     * @tparam Cols
     */
    template<typename T, size_t Rows, size_t Cols, typename Backend>
    struct matrix<T, Rows, Cols, column_major_tag, Backend> {
        using value_type = T;
        using layout = column_major_tag;
        using backend = Backend;

        static constexpr size_t rows = Rows;
        static constexpr size_t cols = Cols;

        static constexpr matrix_storage storage = matrix_storage::dense;
        static constexpr matrix_layout_kind matrix_layout = matrix_layout_kind::column_major;

        buffer_type_t<Backend, T> data;

        T& operator()(size_t r, size_t c)
        requires std::is_same_v<Backend, cpu_backend> {
            return data[c * Rows + r];
        }

        const T& operator()(size_t c, size_t r) const
        requires std::is_same_v<Backend, cpu_backend> {
            return data[c * Rows + r];
        }
    };

    /**
     * Sparse matrix, Compressed Sparse Row
     * @tparam T
     * @tparam Rows
     * @tparam Cols
     */
    template<typename T, size_t Rows, size_t Cols, typename Backend>
    struct matrix<T, Rows, Cols, csr_tag, Backend> {
        using value_type = T;
        using layout = csr_tag;
        using backend = Backend;

        static constexpr size_t rows = Rows;
        static constexpr size_t cols = Cols;

        static constexpr matrix_storage storage = matrix_storage::sparse;
        static constexpr matrix_layout_kind matrix_layout = matrix_layout_kind::csr;

        buffer_type_t<Backend, T> data;
        buffer_type_t<Backend, uint32_t> col_indices;
        buffer_type_t<Backend, uint32_t> row_offsets;
    };

    /**
     * Sparse matrix, Compressed Sparse Column
     * @tparam T
     * @tparam Rows
     * @tparam Cols
     */
    template<typename T, size_t Rows, size_t Cols, typename Backend>
    struct matrix<T, Rows, Cols, csc_tag, Backend> {
        using value_type = T;
        using layout = csc_tag;
        using backend = Backend;

        static constexpr size_t rows = Rows;
        static constexpr size_t cols = Cols;

        static constexpr matrix_storage storage = matrix_storage::sparse;
        static constexpr matrix_layout_kind matrix_layout = matrix_layout_kind::csc;

        buffer_type_t<Backend, T> data;
        buffer_type_t<Backend, uint32_t> row_indices;
        buffer_type_t<Backend, uint32_t> col_offsets;
    };

    template<typename M>
    inline constexpr bool is_dense_v = M::storage == matrix_storage::dense;

    template<typename M>
    inline constexpr bool is_sparse_v = M::storage == matrix_storage::sparse;

    template<typename M>
    inline constexpr bool is_row_major_v = M::matrix_layout == matrix_layout_kind::row_major;

    template<typename M>
    inline constexpr bool is_csr_v = M::matrix_layout == matrix_layout_kind::csr;

    template<typename T, size_t Rows, size_t Cols, typename Backend = cpu_backend>
    using dense_row_matrix = matrix<T, Rows, Cols, row_major_tag, Backend>;

    template<typename T, size_t Rows, size_t Cols, typename Backend = cpu_backend>
    using dense_column_matrix = matrix<T, Rows, Cols, column_major_tag, Backend>;

    template<typename T, size_t Rows, size_t Cols, typename Backend = cpu_backend>
    using sparse_csr_matrix = matrix<T, Rows, Cols, csr_tag, Backend>;

    template<typename T, size_t Rows, size_t Cols, typename Backend = cpu_backend>
    using sparse_csc_matrix = matrix<T, Rows, Cols, csc_tag, Backend>;
}
