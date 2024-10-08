#pragma once

class FixedUpdate{
public:
    FixedUpdate() = default;

    explicit FixedUpdate(int fps)
    : _frequency(fps)
    , _period(fps != 0 ? 1/static_cast<float>(fps) : 0)
    {}

    inline void operator()(auto body) {
        if(!_paused && _elapsedTime > _period) {
            body();
            _updates++;
            _frames++;
            _elapsedTime = 0;
        }
    }

    inline void advance(float time) {
        if(_paused) return;
        static float sElapsedTimeSeconds = 0;
        sElapsedTimeSeconds += time;
        _elapsedTime += time;

        if(sElapsedTimeSeconds > 1) {
            _updatesPerSecond = _updates;
            sElapsedTimeSeconds = 0;
            _updates = 0;
        }
    }

    inline void frequency(int fps) {
        _frequency = fps;
        _period = fps != 0 ? 1/static_cast<float>(fps) : 0;
        _elapsedTime = 0;
        _updates = 0;
        _updatesPerSecond = 0;
        _frames = 0;
    }

    [[nodiscard]] inline int frequency() const {
        return _frequency;
    }

    [[nodiscard]]
    inline float period() const {
        return _period;
    }

    [[nodiscard]]
    inline int framesPerSecond() const {
        return _updatesPerSecond;
    }

    [[nodiscard]]
    inline uint64_t frames() const {
        return _frames;
    }

    inline void pause() {
        _paused = true;
    }

    inline void unPause() {
        _paused = false;
    }

    [[nodiscard]]
    inline bool paused() const {
        return _paused;
    }

private:
    int _frequency{};
    int _updates{};
    int _updatesPerSecond{};
    uint64_t _frames{};
    bool _paused{};
    float _elapsedTime{};
    float _period{};
};