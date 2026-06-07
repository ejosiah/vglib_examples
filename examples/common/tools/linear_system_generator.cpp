#include <euler_spd_generator.hpp>

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4 || argc > 6) {
        std::cerr << "usage: linear_system_generator <output-file> <grid-size> <simulation-time> [time-step] [diffusion]\n";
        return 1;
    }

    try {
        eular::spd_generator::config cfg{};
        cfg.output_file = argv[1];
        cfg.grid_size = static_cast<size_t>(std::stoull(argv[2]));
        cfg.simulation_time = std::stod(argv[3]);

        if (argc > 4) {
            cfg.time_step = std::stod(argv[4]);
        }

        if (argc > 5) {
            cfg.diffusion = std::stod(argv[5]);
        }

        const auto system = eular::spd_generator::generate_to_file(cfg);
        std::cout << "wrote " << cfg.output_file.string()
                  << " rows=" << system.rows
                  << " cols=" << system.cols
                  << " non_zeros=" << system.header.at("non_zeros")
                  << " grid_size=" << cfg.grid_size
                  << " simulation_time=" << cfg.simulation_time
                  << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }

    return 0;
}
