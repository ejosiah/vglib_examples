#pragma once

#include <glm/glm.hpp>

#include <cinttypes>
#include <vector>
#include <variant>
#include <stdexcept>

enum class SamplerType : int { Halton = 0, R2, Hammersley, Interleaved_Gradients };

struct Halton {

    glm::vec2 operator()(int32_t index) {
        if(_samples.size() > index) return _samples[index];
        glm::vec2 sample{};
        sample.x = halton(index, 2);
        sample.y = halton(index, 3);
        _samples.push_back(sample);
        return sample;
    }

private:
    std::vector<glm::vec2> _samples;

    static float halton(int32_t i, int32_t b){
        float f = 1.0f;
        float r = 0.0f;
        while(i > 0) {
            f = f / static_cast<float>( b );
            r = r + f * static_cast<float>( i % b );
            i = i / b;
        }
        return r;
    };
};

struct R2 {
    glm::vec2 operator()(int32_t index) {
        if(_samples.size() > index) return _samples[index];

        const float g = 1.32471795724474602596f;
        const float a1 = 1.0f / g;
        const float a2 = 1.0f / (g * g);

        const float x = fmod( 0.5f + a1 * index, 1.0f );
        const float y = fmod( 0.5f + a2 * index, 1.0f );
        auto sample = glm::vec2{ x, y };
        _samples.push_back(sample);

        return sample;
    }

private:
    std::vector<glm::vec2> _samples;
};

struct Hammersley {
    int numSamples{100};
    glm::vec2 operator()(int32_t index) {
        return glm::vec2{ index * 1.f / numSamples, radical_inverse_base2( uint32_t( index ) ) };
    }
    
private:
    inline float radical_inverse_base2( uint32_t bits ) {
        bits = ( bits << 16u ) | ( bits >> 16u );
        bits = ( ( bits & 0x55555555u ) << 1u ) | ( ( bits & 0xAAAAAAAAu ) >> 1u );
        bits = ( ( bits & 0x33333333u ) << 2u ) | ( ( bits & 0xCCCCCCCCu ) >> 2u );
        bits = ( ( bits & 0x0F0F0F0Fu ) << 4u ) | ( ( bits & 0xF0F0F0F0u ) >> 4u );
        bits = ( ( bits & 0x00FF00FFu ) << 8u ) | ( ( bits & 0xFF00FF00u ) >> 8u );
        return float( bits ) * 2.3283064365386963e-10f; // / 0x100000000
    }
};

struct InterleavedGradient {
    glm::vec2 operator()(int32_t index) {
        return { interleaved_gradient_noise({1, 1}, index), interleaved_gradient_noise({1, 2}, index) };
    }

private:
    float interleaved_gradient_noise( glm::vec2 pixel, int32_t index ) {
        pixel =  pixel + float( index ) * 5.588238f;
        const float noise = fmodf( 52.9829189f * fmodf( 0.06711056f * pixel.x + 0.00583715f * pixel.y, 1.0f ), 1.0f );
        return noise;
    }

};

struct Sampler {
    friend class Jitter;

    SamplerType type{SamplerType::Halton};

    void warmUp(size_t maxSamples) {
        for(auto i = 0; i < maxSamples; ++i) {
            halton(i);
            r2(i);
        }
    }

    glm::vec2 sampleAt(int32_t index) {
        if(type == SamplerType::Halton) {
            return halton(index);
        }else if(type == SamplerType::R2) {
            return r2(index);
        }else if(type == SamplerType::Hammersley) {
            return hammersley(index);
        }else if(type == SamplerType::Interleaved_Gradients) {
            return ig(index);
        }

        throw std::runtime_error{"Unknown sampler"};
    }

private:
    R2 r2;
    InterleavedGradient ig;
    Halton halton;
    Hammersley hammersley;
};

struct Jitter{
    explicit Jitter(size_t sampleCount = 16) {
        period(_period);
        sampler.warmUp(sampleCount);
    }

    void period(int value) {
        _period = value;
        sampler.hammersley.numSamples = value;
    }

    Sampler sampler{};

    glm::vec2 nextSample() {
        const auto sample = sampler.sampleAt(_index);
        _index = (_index + 1) % _period;
        return sample;
    }
private:
    int32_t _index{};
    int32_t _period{8};
};