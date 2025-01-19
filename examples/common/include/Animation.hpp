#pragma once

#include <vector>
#include <cinttypes>
#include <chrono>
#include <cassert>
#include <spdlog/spdlog.h>

template<typename T>
struct Frame {
    uint64_t id;
    T object;
    std::chrono::milliseconds duration;
};

template<typename T>
class Animation {
public:
    Animation() = default;

    explicit Animation(size_t numFrames)
    : numFrames_(numFrames){
        frames_.reserve(numFrames);
    }

    void addFrame(T object, std::chrono::milliseconds duration) {
        static uint64_t id = 0;
        assert(frames_.size() < numFrames_);
        frames_.emplace_back(id++, object, duration);
    }

    bool next(float deltaTimeInSeconds) {
        static float elapsedTime = 0;
        elapsedTime += deltaTimeInSeconds * 1000;
        if(elapsedTime > runtime_) {
            runtime_ = elapsedTime + frames_[frameIndex_].duration.count();

            if(frameIndex_ < frames_.size()) {
                if(loop_) {
                    frameIndex_ = ++frameIndex_%numFrames_;
                }else {
                    frameIndex_ = std::min(++frameIndex_, numFrames_);
                }
                return true;
            }
        }
        return false;
    }

    void toggleLoop() {
        loop_ = !loop_;
    }

    void reset() {
        frameIndex_ = 0;
        runtime_ = 0;
    }

    T& current()  {
        return frames_[frameIndex_].object;
    }

    auto currentFrameIndex() {
        return frameIndex_;
    }

    T& get(int index)  {
        assert(index < frames_.size());
        return frames_[index].object;
    }

    T& operator[](int index) {
        assert(index < frames_.size());
        return frames_[index].object;
    };

    T& get(int index) const  {
        assert(index < frames_.size());
        return frames_[index].object;
    }

    T& operator[](int index) const {
        assert(index < frames_.size());
        return frames_[index].object;
    };

    auto frameCount() const {
        return frames_.size();
    }

private:
    std::vector<Frame<T>> frames_;
    size_t numFrames_{};
    size_t frameIndex_{};
    bool loop_{};
    long runtime_{};

};