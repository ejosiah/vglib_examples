#pragma once

#include <cpr/cpr.h>
#include <stdexcept>
#include <filemanager.hpp>
#include <filemanager.hpp>
#include <nlohmann/json.hpp>

#include <tuple>

namespace geo_location {

    std::string get_api_key() {
        auto key = std::getenv("IP_API_KEY");
        if (!key) {
            throw std::runtime_error{ "IP_API_KEY environment variable not set" };
        }
        return key;
    }

    inline std::tuple<double, double> get() {
        auto& fileManager = FileManager::instance();
        auto cache = fileManager.getFullPath("geo_location.json");

        std::string raw_string;
        if (cache.has_value()) {
            spdlog::info("retrieving current location from local cache");
            auto data = fileManager.load("geo_location.json", false);
            raw_string = std::string{data.begin(), data.end()};

            // Don't work when VPN is on
            // const auto json = nlohmann::json::parse(raw_string);
            // const auto url = cpr::Url{"https://checkip.amazonaws.com/"};
            // auto response = cpr::Get(url, cpr::VerifySsl{false});
            //
            // if (response.status_code == 200) {
            //     const std::string current_ip = response.text.replace(response.text.find("\n"), 1, "");
            //     const std::string cached_ip = json["ip"];
            //     if (current_ip != cached_ip) {
            //         spdlog::info("cached location doesn't match current location ip, will refresh cache");
            //         raw_string = "";
            //     }
            // }
        }

        if (raw_string.empty()) {
            spdlog::info("retrieving current location from external api");
            const auto api_key = get_api_key();
            const auto url = cpr::Url{fmt::format("{}{}", "http://api.ipapi.com/check?access_key=", api_key)};
            auto response = cpr::Get(url);

            if (response.status_code != 200) {
                throw std::runtime_error{ "unable to retrieve current current location" };
            }
            raw_string = response.text;
            FileManager::save(raw_string, "../data/geo_location.json", false);
        }

        const auto json = nlohmann::json::parse(raw_string);
        const double latitude = json["latitude"];
        const double longitude = json["longitude"];

        return std::make_tuple(latitude, longitude);
    }

}