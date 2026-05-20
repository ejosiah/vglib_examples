#pragma once

#include "geo_location.hpp"
#include <cmath>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sun_calc {

struct position {
    double azimuth{};
    double altitude{};
};

template <typename time_point_t>
struct sun_times {
    std::map<std::string, time_point_t> values;
};

struct moon_position {
    double azimuth{};
    double altitude{};
    double distance{};
    double parallactic_angle{};
};

struct moon_illumination {
    double fraction{};
    double phase{};
    double angle{};
};

template <typename time_point_t>
struct moon_times {
    std::optional<time_point_t> rise;
    std::optional<time_point_t> set;
    bool always_up{false};
    bool always_down{false};
};

struct time_def {
    double angle;
    std::string rise_name;
    std::string set_name;
};

inline std::vector<time_def>& times() {
    static std::vector<time_def> data = {
        {-0.833, "sunrise", "sunset"},
        {-0.3,   "sunrise_end", "sunset_start"},
        {-6.0,   "dawn", "dusk"},
        {-12.0,  "nautical_dawn", "nautical_dusk"},
        {-18.0,  "night_end", "night"},
        {6.0,    "golden_hour_end", "golden_hour"}
    };

    return data;
}

inline void add_time(double angle,
                     std::string rise_name,
                     std::string set_name) {
    times().push_back({
        angle,
        std::move(rise_name),
        std::move(set_name)
    });
}

namespace detail {

constexpr double pi = 3.14159265358979323846;
constexpr double rad = pi / 180.0;
constexpr double day_ms = 1000.0 * 60.0 * 60.0 * 24.0;
constexpr double j1970 = 2440588.0;
constexpr double j2000 = 2451545.0;
constexpr double e = rad * 23.4397;
constexpr double j0 = 0.0009;

template <typename time_point_t>
inline double to_julian(time_point_t date) {
    auto ms = std::chrono::duration<double, std::milli>(
        date.time_since_epoch()
    ).count();

    return ms / day_ms - 0.5 + j1970;
}

template <typename time_point_t>
inline time_point_t from_julian(double j) {
    auto ms = (j + 0.5 - j1970) * day_ms;

    return time_point_t{
        std::chrono::duration_cast<typename time_point_t::duration>(
            std::chrono::duration<double, std::milli>(ms)
        )
    };
}

template <typename time_point_t>
inline double to_days(time_point_t date) {
    return to_julian(date) - j2000;
}

inline double right_ascension(double l, double b) {
    return std::atan2(
        std::sin(l) * std::cos(e) -
        std::tan(b) * std::sin(e),
        std::cos(l)
    );
}

inline double declination(double l, double b) {
    return std::asin(
        std::sin(b) * std::cos(e) +
        std::cos(b) * std::sin(e) * std::sin(l)
    );
}

inline double azimuth(double h, double phi, double dec) {
    return std::atan2(
        std::sin(h),
        std::cos(h) * std::sin(phi) -
        std::tan(dec) * std::cos(phi)
    );
}

inline double altitude(double h, double phi, double dec) {
    return std::asin(
        std::sin(phi) * std::sin(dec) +
        std::cos(phi) * std::cos(dec) * std::cos(h)
    );
}

inline double sidereal_time(double d, double lw) {
    return rad * (280.16 + 360.9856235 * d) - lw;
}

inline double astro_refraction(double h) {
    if (h < 0.0) {
        h = 0.0;
    }

    return 0.0002967 /
           std::tan(h + 0.00312536 / (h + 0.08901179));
}

inline double solar_mean_anomaly(double d) {
    return rad * (357.5291 + 0.98560028 * d);
}

inline double ecliptic_longitude(double m) {
    double c = rad * (
        1.9148 * std::sin(m) +
        0.02   * std::sin(2.0 * m) +
        0.0003 * std::sin(3.0 * m)
    );

    double p = rad * 102.9372;

    return m + c + p + pi;
}

struct sun_coords {
    double dec{};
    double ra{};
};

inline sun_coords get_sun_coords(double d) {
    double m = solar_mean_anomaly(d);
    double l = ecliptic_longitude(m);

    return {
        declination(l, 0.0),
        right_ascension(l, 0.0)
    };
}

inline double julian_cycle(double d, double lw) {
    return std::round(d - j0 - lw / (2.0 * pi));
}

inline double approx_transit(double ht, double lw, double n) {
    return j0 + (ht + lw) / (2.0 * pi) + n;
}

inline double solar_transit_j(double ds,
                              double m,
                              double l) {
    return j2000 + ds +
           0.0053 * std::sin(m) -
           0.0069 * std::sin(2.0 * l);
}

inline double hour_angle(double h,
                         double phi,
                         double d) {
    return std::acos(
        (std::sin(h) -
         std::sin(phi) * std::sin(d)) /
        (std::cos(phi) * std::cos(d))
    );
}

inline double observer_angle(double height) {
    return -2.076 * std::sqrt(height) / 60.0;
}

inline double get_set_j(double h,
                        double lw,
                        double phi,
                        double dec,
                        double n,
                        double m,
                        double l) {
    double w = hour_angle(h, phi, dec);
    double a = approx_transit(w, lw, n);

    return solar_transit_j(a, m, l);
}

struct moon_coords {
    double ra{};
    double dec{};
    double dist{};
};

inline moon_coords get_moon_coords(double d) {
    double l = rad * (218.316 + 13.176396 * d);
    double m = rad * (134.963 + 13.064993 * d);
    double f = rad * (93.272 + 13.229350 * d);

    double lon = l + rad * 6.289 * std::sin(m);
    double lat = rad * 5.128 * std::sin(f);
    double dist = 385001.0 - 20905.0 * std::cos(m);

    return {
        right_ascension(lon, lat),
        declination(lon, lat),
        dist
    };
}

template <typename time_point_t>
inline time_point_t hours_later(time_point_t date, double h) {
    auto ms = h * day_ms / 24.0;

    return date +
           std::chrono::duration_cast<typename time_point_t::duration>(
               std::chrono::duration<double, std::milli>(ms)
           );
}

template <typename time_point_t>
inline time_point_t floor_to_utc_day(time_point_t date) {
    auto secs =
        std::chrono::duration_cast<std::chrono::seconds>(
            date.time_since_epoch()
        );

    auto days = secs.count() / 86400;

    if (secs.count() < 0 &&
        secs.count() % 86400 != 0) {
        --days;
    }

    return time_point_t{
        std::chrono::duration_cast<typename time_point_t::duration>(
            std::chrono::seconds(days * 86400)
        )
    };
}

} // namespace detail

template <typename time_point_t>
inline position get_position(time_point_t date,
                             double lat,
                             double lng) {
    double lw = detail::rad * -lng;
    double phi = detail::rad * lat;
    double d = detail::to_days(date);

    auto c = detail::get_sun_coords(d);
    double h = detail::sidereal_time(d, lw) - c.ra;

    return {
        detail::azimuth(h, phi, c.dec),
        detail::altitude(h, phi, c.dec)
    };
}

template <typename time_point_t>
inline sun_times<time_point_t> get_times(time_point_t date,
                                         double lat,
                                         double lng,
                                         double height = 0.0) {
    double lw = detail::rad * -lng;
    double phi = detail::rad * lat;
    double dh = detail::observer_angle(height);
    double d = detail::to_days(date);

    double n = detail::julian_cycle(d, lw);
    double ds = detail::approx_transit(0.0, lw, n);
    double m = detail::solar_mean_anomaly(ds);
    double l = detail::ecliptic_longitude(m);
    double dec = detail::declination(l, 0.0);

    double j_noon =
        detail::solar_transit_j(ds, m, l);

    sun_times<time_point_t> result;

    result.values["solar_noon"] =
        detail::from_julian<time_point_t>(j_noon);

    result.values["nadir"] =
        detail::from_julian<time_point_t>(j_noon - 0.5);

    for (const auto& time : times()) {
        double h0 =
            (time.angle + dh) * detail::rad;

        double j_set =
            detail::get_set_j(
                h0,
                lw,
                phi,
                dec,
                n,
                m,
                l
            );

        double j_rise =
            j_noon - (j_set - j_noon);

        result.values[time.rise_name] =
            detail::from_julian<time_point_t>(j_rise);

        result.values[time.set_name] =
            detail::from_julian<time_point_t>(j_set);
    }

    return result;
}

template <typename time_point_t>
inline moon_position get_moon_position(time_point_t date,
                                       double lat,
                                       double lng) {
    double lw = detail::rad * -lng;
    double phi = detail::rad * lat;
    double d = detail::to_days(date);

    auto c = detail::get_moon_coords(d);

    double h =
        detail::sidereal_time(d, lw) - c.ra;

    double alt =
        detail::altitude(h, phi, c.dec);

    double pa = std::atan2(
        std::sin(h),
        std::tan(phi) * std::cos(c.dec) -
        std::sin(c.dec) * std::cos(h)
    );

    return {
        detail::azimuth(h, phi, c.dec),
        alt + detail::astro_refraction(alt),
        c.dist,
        pa
    };
}

template <typename time_point_t>
inline moon_illumination get_moon_illumination(time_point_t date) {
    double d = detail::to_days(date);

    auto s = detail::get_sun_coords(d);
    auto m = detail::get_moon_coords(d);

    double sdist = 149598000.0;

    double phi = std::acos(
        std::sin(s.dec) * std::sin(m.dec) +
        std::cos(s.dec) * std::cos(m.dec) *
        std::cos(s.ra - m.ra)
    );

    double inc = std::atan2(
        sdist * std::sin(phi),
        m.dist - sdist * std::cos(phi)
    );

    double angle = std::atan2(
        std::cos(s.dec) * std::sin(s.ra - m.ra),
        std::sin(s.dec) * std::cos(m.dec) -
        std::cos(s.dec) * std::sin(m.dec) *
        std::cos(s.ra - m.ra)
    );

    return {
        (1.0 + std::cos(inc)) / 2.0,
        0.5 +
            0.5 * inc *
            (angle < 0.0 ? -1.0 : 1.0) /
            detail::pi,
        angle
    };
}

template <typename time_point_t>
inline moon_times<time_point_t> get_moon_times(time_point_t date,
                                               double lat,
                                               double lng) {
    time_point_t t = detail::floor_to_utc_day(date);

    constexpr double hc =
        0.133 * detail::rad;

    double h0 =
        get_moon_position(t, lat, lng)
            .altitude - hc;

    std::optional<double> rise;
    std::optional<double> set;

    double ye = 0.0;

    for (int i = 1; i <= 24; i += 2) {

        double h1 =
            get_moon_position(
                detail::hours_later(t, i),
                lat,
                lng
            ).altitude - hc;

        double h2 =
            get_moon_position(
                detail::hours_later(t, i + 1),
                lat,
                lng
            ).altitude - hc;

        double a =
            (h0 + h2) / 2.0 - h1;

        double b =
            (h2 - h0) / 2.0;

        double xe =
            -b / (2.0 * a);

        double d =
            b * b - 4.0 * a * h1;

        int roots = 0;

        double x1 = 0.0;
        double x2 = 0.0;

        ye =
            (a * xe + b) * xe + h1;

        if (d >= 0.0) {

            double dx =
                std::sqrt(d) /
                (std::abs(a) * 2.0);

            x1 = xe - dx;
            x2 = xe + dx;

            if (std::abs(x1) <= 1.0) {
                roots++;
            }

            if (std::abs(x2) <= 1.0) {
                roots++;
            }

            if (x1 < -1.0) {
                x1 = x2;
            }
        }

        if (roots == 1) {

            if (h0 < 0.0) {
                rise = i + x1;
            } else {
                set = i + x1;
            }

        } else if (roots == 2) {

            rise =
                i + (ye < 0.0 ? x2 : x1);

            set =
                i + (ye < 0.0 ? x1 : x2);
        }

        if (rise && set) {
            break;
        }

        h0 = h2;
    }

    moon_times<time_point_t> result;

    if (rise) {
        result.rise =
            detail::hours_later(t, *rise);
    }

    if (set) {
        result.set =
            detail::hours_later(t, *set);
    }

    if (!rise && !set) {

        if (ye > 0.0) {
            result.always_up = true;
        } else {
            result.always_down = true;
        }
    }

    return result;
}

template <typename time_point_t>
inline position get_sun_position_from_current_location(time_point_t date) {
    auto [latitude, longitude] = geo_location::get();
    return get_position(date, latitude, longitude);
}

template <typename time_point_t>
inline moon_position get_moon_position_from_current_location(time_point_t date) {
    auto [latitude, longitude] = geo_location::get();
    return get_moon_position(date, latitude, longitude);
}

} // namespace sun_calc