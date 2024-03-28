#pragma once

class FixedUpdate{
public:
    FixedUpdate() = default;

    FixedUpdate(int fps)
    : _frequency(fps)
    , _period(fps != 0 ? 1/static_cast<float>(fps) : 0)
    {}

    inline void operator()(auto body) {
        if(_elapsedTime > _period) {
            body();
            _updates++;
            _elapsedTime = 0;
        }
    }

    inline void update(float time) {
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
    }

    inline int frequency() const {
        return _frequency;
    }

    [[nodiscard]]
    inline float period() const {
        return _period;
    }

    [[nodiscard]]
    inline int framesPerSecond() {
        return _updatesPerSecond;
    }

private:
    int _frequency{};
    int _updates{};
    int _updatesPerSecond{};
    float _elapsedTime{};
    float _period{};
};