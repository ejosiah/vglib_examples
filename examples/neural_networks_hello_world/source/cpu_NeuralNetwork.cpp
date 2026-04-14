#include "cpu/NeuralNetwork.hpp"
#include <chrono>
#include <random>
#include <spdlog/spdlog.h>
#include <numeric>

namespace cpu {

NeuralNetwork::NeuralNetwork(std::initializer_list<uint> layers, bool testMode)
    :m_layers{layers.begin(), layers.end()}
    ,m_numLayers {static_cast<int>(layers.size())}
    , m_testMode{testMode} {

    std::normal_distribution<float> distribution{0.0f, 1.0f};
    std::default_random_engine generator{ testMode ? 1 << 20 : std::random_device{}()};
    auto rng = [distribution, generator] mutable { return distribution(generator); };

    for (auto l = 0; l < m_numLayers - 1; l++) {
        auto L = static_cast<int>(m_layers[l]);
        auto L1 = static_cast<int>(m_layers[l + 1]);
        nda::matrix<float> w{{L1, L}, 0};
        nda::matrix<float> b{{L1, 1}, 0};

        nda::for_all_indices(w.shape(), [&](auto i, auto j) {
            w(i, j) = rng();
        });

        nda::for_all_indices(b.shape(), [&](auto i, auto j) {
            b(i, j) = rng();
        });

        m_weights.push_back(std::move(w));
        m_biases.push_back(std::move(b));
    }
}

void NeuralNetwork::train(Dataset &trainingData, uint epochs, uint batchSize, float eta,
    std::optional<std::reference_wrapper<const Dataset>> testData) {
    const auto start = std::chrono::steady_clock::now();

    spdlog::info("training network with {} inputs with mini batch size of {} for {} epochs at {} training rate"
        , trainingData.size(), batchSize, epochs, eta);

    for (auto j  = 0; j < epochs; j++) {
        shuffle(trainingData);
        auto numBatches = trainingData.size() / batchSize;

        auto nextBatch = trainingData.data();
        for (auto k = 0; k < numBatches; k++) {
            auto batch = std::span{ nextBatch, batchSize };
            update(batch, eta);
            nextBatch += batchSize;
        }
        if (testData.has_value()) {
            auto total = evaluate(testData.value()) * 100.f;
            auto n = static_cast<float>(testData.value().get().size());
            spdlog::info("Epoch {}: {}%", j, total/n);
        }else {
            spdlog::info("Epoch {} complete", j);
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(end - start).count();
    spdlog::info("training completed in {:.3f}s", elapsed);
}

NeuralNetwork::Activation NeuralNetwork::feedForward(const Activation& a) const {
    auto activation = a;
    for (auto l = 0; l < m_numLayers - 1; l++) {
        auto& w = m_weights[l];
        auto& b = m_biases[l];
        auto z = dot(w, activation);
        z = add(z, b);
        activation = sigmoid(z);
    }
    return activation;
}

float NeuralNetwork::evaluate(const Dataset &dataset) const {

    auto results = map_range(dataset, [&](auto& e) {
        auto& [a, y] = e;
        auto aa = multiply(feedForward(a), y);

        float total = 0;

        enum { i, j };
        ein_reduce(nda::ein(total) += nda::ein<i, j>(aa));
        return total > 0.5;
    });
    return std::reduce(results.begin(), results.end(), 0.0f);
}

void NeuralNetwork::update(std::span<Entry> batch, float eta) {
    auto nabla_ws = map_range(m_weights, [](auto& w){ return nda::matrix<float>(w.shape(), 0.0f); });
    auto nabla_bs = map_range(m_biases, [](auto& b){ return nda::matrix<float>(b.shape(), 0.0f); });


    for (auto& [x, y] : batch) {
        auto [d_nabla_w, d_nabla_b]  = backpropagate(x, y);

        for (auto l = 0; l < m_numLayers - 1; l++) {
            auto& nabla_w = nabla_ws[l];
            auto& nabla_b = nabla_bs[l];

            nda::for_all_indices(nabla_w.shape(), [&](auto i, auto j) {
                nabla_w(i, j) += d_nabla_w[l](i, j);
            });

            nda::for_all_indices(nabla_b.shape(), [&](auto i, auto j) {
               nabla_b(i, j) += d_nabla_b[l](i, j);
            });
        }
    }

    const auto m = static_cast<float>(batch.size());
    for (auto l = 0; l < m_numLayers - 1; l++) {
        auto& w = m_weights[l];
        auto& b = m_biases[l];
        auto& nw = nabla_ws[l];
        auto& nb = nabla_bs[l];

        nda::for_all_indices(w.shape(), [&](auto i, auto j) {
            w(i, j) -= nw(i, j) * eta/m;
        });

        nda::for_all_indices(b.shape(), [&](auto i, auto j) {
           b(i, j) -= nb(i, j) * eta/m;
        });
    }

}

NeuralNetwork::WeightsAndBiases NeuralNetwork::backpropagate(const Image& x, const Label& y) {
    auto zs = map_range(m_layers.begin() + 1, m_layers.end(), [](auto L){ return nda::matrix<float>({L, 1}, 0); });
    auto as = map_range(m_layers, [](auto L){ return nda::matrix<float>({L, 1}, 0); });
    as[0] = x;

    // feed forward
    for (auto l = 0; l < m_numLayers - 1; l++) {
        auto& a = as[l];
        auto& w = m_weights[l];
        auto& b = m_biases[l];
        auto z = dot(w, a);
        z = add(z, b);
        as[l + 1] = sigmoid(z);
        zs[l] = z;
    }

    // back propagation
    auto nabla_w = map_range(m_weights, [](auto& w){ return nda::matrix<float>(w.shape(), 0.0f); });
    auto nabla_b = map_range(m_biases, [](auto& b){ return nda::matrix<float>(b.shape(), 0.0f); });
    auto deltas = map_range(m_layers, [](auto L){ return nda::matrix<float>({L, 1}, 0); });

    const auto L = m_numLayers - 1;
    const auto delta = multiply(cost_derivative(as[L], y), sigmoid_prime(zs[L-1]));
    deltas[L] = delta;
    nabla_b[L-1] = delta;

    auto aT = transpose(as[L-1]);
    nabla_w[L-1] = dot(delta, aT);


    for (auto l = m_numLayers - 2; l > 0; l--) {
        const auto& deltaL = deltas[l+1];
        const auto& zp = sigmoid_prime(zs[l-1]);
        const auto& wT = transpose(m_weights[l]);
        const auto w_dot_d = dot(wT, deltaL);
        const auto delta = multiply(w_dot_d, zp);
        deltas[l] = delta;
        nabla_b[l-1] = delta;

        const auto aT = transpose(as[l-1]);
        nabla_w[l-1] = dot(delta, aT);
    }


    return std::make_tuple(nabla_w, nabla_b);
}

void NeuralNetwork::shuffle(Dataset &dataset) const {
    if (m_testMode) return;
    static std::default_random_engine generator{ 1 << 20 };

    std::shuffle(std::begin(dataset), std::end(dataset), generator);
}
}
