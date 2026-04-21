#pragma once

#include "array/matrix.h"
#include "array/ein_reduce.h"
#include "mnist/mnist_loader.hpp"

#include <tuple>
#include <span>
#include <algorithm>

template <typename T>
inline nda::matrix<T> dot(const nda::matrix<T>& A, const nda::matrix<T>& B) {
    assert(A.j().extent() == B.i().extent());

    enum { i = 0, j = 1, k = 2 };

    auto a = A.base();
    auto b = B.base();

    nda::matrix<T> C({A.i().extent(), B.j().extent()}, T{});
    ein_reduce(ein<i, j>(C) += ein<i, k>(A) * ein<k, j>(B));
    return C;
}

template <typename T, typename Function>
inline auto apply(Function func, nda::matrix<T> x) {
    using R = std::decay_t<std::invoke_result_t<Function, const T&>>;

    nda::matrix<R> y({x.i().extent(), x.j().extent()});

    for_all_indices(x.shape(), [&](auto i, auto j) {
      y(i, j) = func(x(i, j));
    });

    return y;
}

template <typename T>
inline nda::matrix<T> transpose(const nda::matrix<T>& A) {
    nda::matrix<T> B({A.j().extent(), A.i().extent()}, T{});

    for_all_indices(A.shape(), [&](auto i, auto j) {
        B(j, i) = A(i, j);
    });

    return B;
}

template <typename T>
inline auto sigmoid(nda::matrix<T> X) {
    for_all_indices(X.shape(), [&](auto i, auto j) {
        auto x = X(i, j);
        X(i, j) = T{1.f} / (T{1.0} + std::exp(-x));
    });
    return X;
}

template <typename T>
std::tuple<int, int> argmax(nda::matrix<T> a) {
    int s = 0, t = 0;

    T maxValue{std::numeric_limits<T>::lowest()};

    std::numeric_limits<T>::min();
    for_all_indices(a.shape(), [&](auto i, auto j) {
        auto x = a(i, j);
        if (maxValue < x) {
            maxValue = x;
            s = i; t = j;
        }
    });
    return std::make_tuple(s, t);
}

inline size_t argmax(std::span<const float> v) {
    return std::distance(v.begin(), std::max_element(v.begin(), v.end()));
}

template <typename T>
inline auto sigmoid_prime(nda::matrix<T> X) {
    for_all_indices(X.shape(), [&](auto i, auto j) {
        auto x = X(i, j);
        auto sigmoid = T{1.f}/(T{1.f} + std::exp(-x));
        X(i, j) = (T{1.f} - sigmoid) * sigmoid;
    });

    return X;
}

template <typename T>
inline auto cost_derivative(const nda::matrix<T>& A, const nda::matrix<T>& Y) {
    assert(Y.i().extent() == A.i().extent());
    assert(Y.j().extent() == A.j().extent());

    nda::matrix<T> R({A.i().extent(), A.j().extent()});

    for_all_indices(A.shape(), [&](auto i, auto j) {
      R(i, j) = A(i, j) - Y(i, j);
    });

    return R;
}

template <typename T>
inline nda::matrix<T> add(const nda::matrix<T>& A, const nda::matrix<T>& B) {
    assert(A.i().extent() == B.i().extent());
    assert(A.j().extent() == B.j().extent());

    enum { i = 0, j = 1 };

    nda::matrix<T> C({A.i().extent(), A.j().extent()}, T{});
    ein_reduce(ein<i, j>(C) += ein<i, j>(A) + ein<i, j>(B));
    return C;
}

template <typename T>
inline nda::matrix<T> subtract(const nda::matrix<T>& A, const nda::matrix<T>& B) {
    assert(A.i().extent() == B.i().extent());
    assert(A.j().extent() == B.j().extent());

    enum { i = 0, j = 1 };

    nda::matrix<T> C({A.i().extent(), A.j().extent()}, T{});
    ein_reduce(ein<i, j>(C) += ein<i, j>(A) - ein<i, j>(B));
    return C;
}

template <typename T>
inline nda::matrix<T> multiply(const nda::matrix<T>& A, const nda::matrix<T>& B) {
    assert(A.i().extent() == B.i().extent());
    assert(A.j().extent() == B.j().extent());

    enum { i = 0, j = 1 };

    nda::matrix<T> C({A.i().extent(), A.j().extent()}, T{});
    ein_reduce(ein<i, j>(C) += ein<i, j>(A) * ein<i, j>(B));
    return C;
}

template <typename T>
inline nda::matrix<T> divide(const nda::matrix<T>& A, const nda::matrix<T>& B) {
    assert(A.i().extent() == B.i().extent());
    assert(A.j().extent() == B.j().extent());

    enum { i = 0, j = 1 };

    nda::matrix<T> C({A.i().extent(), A.j().extent()}, T{});
    ein_reduce(ein<i, j>(C) += ein<i, j>(A) / ein<i, j>(B));
    return C;
}

template <typename T, typename S>
inline auto scalar_multiply(nda::matrix<T> A, S scalar) {
    enum { i, j };
    return make_ein_sum<T, i, j>(ein<i, j>(A) * static_cast<T>(scalar));
}

inline auto to_matrix(const mnist::Dataset& dataset) {
    std::vector<nda::matrix<float>> images(dataset.header.num_images);
    std::vector<nda::matrix<float>> labels(dataset.header.num_images);

    auto imageSize = dataset.header.cols * dataset.header.rows;
    auto pos = 0;
    for (auto offset = 0u; offset < dataset.images.size(); offset += imageSize) {
        nda::matrix<float> image{{imageSize, 1}, 0};
        nda::for_all_indices(image.shape(), [&](auto i, auto j) {
            image(i, j) = dataset.images[offset + i];
        });
        images[pos++] = image;
    }

    pos = 0;
    for (auto l = 0; l < dataset.labels.size(); l++) {
        nda::matrix<float> label {{10, 1}};
        nda::for_all_indices<>(label.shape(), [&](auto i, auto j) {
            label(i, j) = static_cast<float>(dataset.labels[l] == i);
        });
        labels[pos++] = label;
    }


    std::vector<std::tuple<nda::matrix<float>, nda::matrix<float>>> res;
    for (auto i = 0; i < dataset.header.num_images; ++i) {
        res.push_back(std::make_tuple(images[i], labels[i]));
    }
    return res;
}

inline auto rngFn(std::default_random_engine::result_type seed) {
    return [dist=std::normal_distribution<float>{0.f, 1.f}, gen=std::default_random_engine{seed}] mutable {
        return dist(gen);
    };
}

