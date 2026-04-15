#pragma once

#include "array/matrix.h"
#include "functions.hpp"

#include <functional>
#include <initializer_list>
#include <optional>

namespace cpu {
    class NeuralNetwork {
        using Image = nda::matrix<float>;
        using Label = nda::matrix<float>;
        using Layer = uint;
        using Activation = nda::matrix<float>;
        using Weight = nda::matrix<float>;
        using Bias = nda::matrix<float>;
        using Layers = std::vector<Layer>;
        using Activations = std::vector<Activation>;
        using Biases = std::vector<Bias>;
        using Weights = std::vector<Weight>;
        using WeightsAndBiases = std::tuple<Weights, Biases>;

        using Entry = std::tuple<Image, Label>;
        using Dataset = std::vector<Entry>;

    public:
        NeuralNetwork() = default;

        NeuralNetwork(std::initializer_list<uint> layers, bool testMode = false);

        void train(Dataset& trainingData, uint epochs, uint batchSize,
            float eta, std::optional<std::reference_wrapper<const Dataset>> testData = {});

        [[nodiscard]]
        Activation feedForward(const Activation& a) const;

        float evaluate(const Dataset& dataset) const;

    protected:
        void update(std::span<Entry> batch, float eta);

        WeightsAndBiases backpropagate(const Image& x, const Label& y);

        void shuffle(Dataset& dataset, uint batchSize) const;

    public:
        Layers m_layers;
        int m_numLayers{};
        Weights m_weights;
        Biases m_biases;
        mutable std::array<Activation, 8> m_activations;
        mutable std::array<Activation, 8> m_z;
        bool m_testMode{};
    };
}
