#pragma once

#include <chrono>
#include <cstdint>
#include <type_traits>

class day_night_cycle {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    using duration = clock::duration;

    day_night_cycle() = default;

    explicit day_night_cycle(double real_minutes_for_full_day, std::uint32_t tick_frequency = 60,
                             std::chrono::hours start_time = std::chrono::hours{12})
        : _real_day_duration(std::chrono::duration<double, std::ratio<60> >{
              real_minutes_for_full_day
          }),
          _tick_interval(std::chrono::duration<double>{
              1.0 / static_cast<double>(tick_frequency)
          }),
          _game_time(start_time),
          _last_tick(clock::now()) {
    }

    void tick() {
        const auto now = clock::now();
        const auto delta = now - _last_tick;

        if (delta < _tick_interval) {
            return;
        }

        _last_tick = now;

        advance(delta);
    }

    [[nodiscard]]
    time_point current_time() const {
        return _epoch + _game_time;
    }

    [[nodiscard]]
    int hour() const {
        using namespace std::chrono;
        return static_cast<int>(duration_cast<hours>(_game_time).count() % 24);
    }

    [[nodiscard]]
    int minute() const {
        using namespace std::chrono;
        return static_cast<int>(duration_cast<minutes>(_game_time).count() % 60);
    }

    [[nodiscard]]
    int second() const {
        using namespace std::chrono;
        return static_cast<int>(duration_cast<seconds>(_game_time).count() % 60);
    }

    [[nodiscard]]
    double normalized_time() const {
        using namespace std::chrono;

        constexpr double full_day_seconds = 24.0 * 60.0 * 60.0;

        const double seconds =
                std::chrono::duration<double>(_game_time).count();

        return seconds / full_day_seconds;
    }

private:
    void advance(duration delta) {
        using namespace std::chrono;

        const double real_seconds = std::chrono::duration<double>(delta).count();
        const double full_day_real_seconds = std::chrono::duration<double>(_real_day_duration).count();
        const double day_fraction = real_seconds / full_day_real_seconds;
        const double game_seconds = day_fraction * 24.0 * 60.0 * 60.0;

        using duration_t = std::remove_cvref_t<decltype(_game_time)>;

        _game_time += std::chrono::duration_cast<duration_t>(
            std::chrono::duration<double>{game_seconds}
        );
        wrap_time();
    }

    void wrap_time() {
        using namespace std::chrono;

        constexpr auto full_day = hours{24};

        while (_game_time >= full_day) {
            _game_time -= full_day;
        }

        while (_game_time < seconds{0}) {
            _game_time += full_day;
        }
    }

private:
    std::chrono::duration<double, std::ratio<60> > _real_day_duration{};
    std::chrono::duration<double> _tick_interval{};

    duration _game_time{};

    time_point _last_tick{};
    time_point _epoch{};
};
